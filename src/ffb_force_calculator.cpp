#include "ffb_force_calculator.h"
#include "math_utils.h"
#include "ffb_hal.h"

namespace SunFFB
{
    float FFBForceCalculator::constant_force_calculator(const EffectBlock& effectBlock) const
    {
        return effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].constantData.magnitude;
    }

    float FFBForceCalculator::ramp_force_calculator(const EffectBlock& effectBlock, float elapsedTime) const
    {
        const SetRampForceReportData& rampData = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].rampData;
        const uint16_t duration = effectBlock.effectData.duration > 0 ? effectBlock.effectData.duration : 1;
        return rampData.rampStart + (rampData.rampEnd - rampData.rampStart) * elapsedTime / float(duration);
    }

    float FFBForceCalculator::periodic_force_calculator(uint8_t effectType, const EffectBlock& effectBlock, uint32_t elapsedTime) const
    {
        const SetPeriodicReportData& periodicData = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].periodicData;

        const int16_t offset = periodicData.offset;
        const int16_t magnitude = periodicData.magnitude;
        const uint16_t phase = periodicData.phase;
        const uint16_t period = periodicData.period > 0 ? periodicData.period : 1;

        // Reduce integer time before converting to float, preserving phase after long uptime.
        float cycle = float(elapsedTime % period) / period + float(phase) / USB_MAX_PHASE;
        cycle -= floorf(cycle);
        float wave = 0.f;
        switch (effectType) {
            case ET_SQUARE: wave = cycle < 0.5f ? 1.f : -1.f; break;
            case ET_SINE: wave = sinf(2.f * float(M_PI) * cycle); break;
            case ET_TRIANGLE: wave = 1.f - 4.f * fabsf(cycle < 0.75f ? cycle - 0.25f : cycle - 1.25f); break;
            case ET_SAWTOOTH_UP: wave = 2.f * cycle - 1.f; break;
            case ET_SAWTOOTH_DOWN: wave = 1.f - 2.f * cycle; break;
            default: return 0.f;
        }
        float amplitude = magnitude;
        if (effectBlock.envelopParameter)
            amplitude = get_envelope(effectBlock.envelopeData, elapsedTime, effectBlock.effectData.duration, amplitude);
        const float force = offset + wave * amplitude;

        return force;
    }

    float FFBForceCalculator::apply_condition(const SetConditionReportData& conditionData, float metric) const
    {
        const int16_t cpOffset = conditionData.cpOffset;
        const int16_t postiveCoeff = conditionData.positiveCoefficient;
        const int16_t negativeCoeff = conditionData.negativeCoefficient;
        const uint16_t deadBand = conditionData.deadBand;

        float force = 0.f;

        const float invRange = 1.f / USB_MAX_MAGNITUDE;
        if(metric < (cpOffset - deadBand) * invRange)
        {
            force = (metric - (cpOffset - deadBand) * invRange) * negativeCoeff;

        }
        else if(metric > (cpOffset + deadBand) * invRange)
        {
            force = (metric - (cpOffset + deadBand) * invRange) * postiveCoeff;

        }

        const float saturation = metric < (cpOffset - deadBand) * invRange ?
            conditionData.negativeSaturation : conditionData.positiveSaturation;
        return clamp(-force, -saturation, saturation);
    }

    void FFBForceCalculator::condition_force_calculator(const EffectBlock& block, const float metrics[NUM_AXIS], const float maxima[NUM_AXIS], float forces[NUM_AXIS]) const
    {
        const auto metric_for = [&](float value) {
            if (block.effectData.effectType == ET_FRICTION)
                return value > 0.02f ? 1.f : (value < -0.02f ? -1.f : 0.f);
            return value;
        };
        if (block.effectData.axisEnable & DIRECTION_ENABLE) {
            if (!(block.conditionBlockFlags & 1)) return;
            float projected = 0.f;
            for (uint8_t i = 0; i < NUM_AXIS; ++i)
                projected += metrics[i] / maxima[i] * block.directionUnitVector[i];
            const float force = apply_condition(block.typeSpecificData[0].conditionData, metric_for(projected));
            for (uint8_t i = 0; i < NUM_AXIS; ++i) forces[i] = force * block.directionUnitVector[i];
        } else {
            for (uint8_t i = 0; i < NUM_AXIS; ++i)
                if ((block.effectData.axisEnable & (1 << i)) && (block.conditionBlockFlags & (1 << i)))
                    forces[i] = apply_condition(block.typeSpecificData[i].conditionData, metric_for(metrics[i] / maxima[i]));
        }
    }

    void FFBForceCalculator::force_calculator(FFBReportHandler& ffbReportHandler, const FFBDeviceInput& ffbDeviceInput, int32_t forces[NUM_AXIS]) const
    {
        if(ffbReportHandler.deviceState == FFBReportHandler::DEVICE_STATE_INIT ||
           ffbReportHandler.devicePaused)
        {
            #pragma unroll
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
                forces[i] = 0;
            return;
        }

        EffectBlock* effectBlocks = ffbReportHandler.get_all_effect_blocks();

        float forcesSum[NUM_AXIS] = {0};
        const uint32_t currentTime = _millis();

        for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            EffectBlock& effectBlock = effectBlocks[i];

            if(ffbReportHandler.is_effect_playing(effectBlock, ffbDeviceInput.inputData.buttons, currentTime))
            {
                const uint8_t effectType = effectBlock.effectData.effectType;
                const uint16_t duration = effectBlock.effectData.duration;
                uint32_t elapsedTime = currentTime - effectBlock.startTime;
                // Quantize to samplePeriod boundaries if the host requested coarse refresh
                // (HID PID Sample Period in ms; 0 = default/full rate).
                const uint16_t samplePeriod = effectBlock.effectData.samplePeriod;
                if(samplePeriod > 1)
                    elapsedTime = (elapsedTime / samplePeriod) * samplePeriod;
                const uint8_t effectGain = effectBlock.effectData.gain;
                if (samplePeriod > 1 && effectBlock.sampleValid && effectBlock.sampleTick == elapsedTime) {
                    for (uint8_t axis = 0; axis < NUM_AXIS; ++axis)
                        forcesSum[axis] += effectBlock.sampledForces[axis] * effectGain / float(USB_MAX_EFFECT_GAIN);
                    continue;
                }
                float sampled[NUM_AXIS] = {0};

                float force = 0;
                float forcesCondition[NUM_AXIS] = {0};

                switch (effectType)
                {
                    case ET_CONSTANT:
                        force = constant_force_calculator(effectBlock);
                    break;
                    
                    case ET_RAMP:
                        force = ramp_force_calculator(effectBlock, elapsedTime);
                    break;

                    case ET_SQUARE:
                    case ET_SINE:
                    case ET_TRIANGLE:
                    case ET_SAWTOOTH_DOWN:
                    case ET_SAWTOOTH_UP:
                        force = periodic_force_calculator(effectType, effectBlock, elapsedTime);
                    break;

                    case ET_SPRING:
                        condition_force_calculator(effectBlock, ffbDeviceInput.get_position(), ffbDeviceInput.get_max_position(), forcesCondition);
                    break;

                    case ET_FRICTION:
                        condition_force_calculator(effectBlock, ffbDeviceInput.get_speed(), ffbDeviceInput.get_max_speed(), forcesCondition);
                    break;

                    case ET_DAMPER:
                        condition_force_calculator(effectBlock, ffbDeviceInput.get_speed(), ffbDeviceInput.get_max_speed(), forcesCondition);
                    break;

                    case ET_INERTIA:
                        condition_force_calculator(effectBlock, ffbDeviceInput.get_acceleration(), ffbDeviceInput.get_max_acceleration(), forcesCondition);
                    break;

                    default:
                        continue;
                }

                switch (effectType)
                {
                    case ET_CONSTANT:
                    case ET_RAMP:
                    case ET_SQUARE:
                    case ET_SINE:
                    case ET_TRIANGLE:
                    case ET_SAWTOOTH_DOWN:
                    case ET_SAWTOOTH_UP:
                    {
                        if(effectBlock.envelopParameter && (effectType == ET_CONSTANT || effectType == ET_RAMP)) {
                            const float base = get_base_magnitude(effectBlock, effectType);
                            const float amplitude = get_envelope(effectBlock.envelopeData, elapsedTime, duration, base);
                            force = base > 0.f ? force * amplitude / base : amplitude;
                        }


                        #pragma unroll
                        for(uint8_t axis = 0; axis < NUM_AXIS; ++axis)
                        {
                            if((effectBlock.effectData.axisEnable & DIRECTION_ENABLE) || ((effectBlock.effectData.axisEnable >> axis) & 0x01))
                                sampled[axis] = force * effectBlock.directionUnitVector[axis];
                        }
                    }
                    break;

                    case ET_SPRING:
                    case ET_FRICTION:
                    case ET_DAMPER:
                    case ET_INERTIA:
                        #pragma unroll
                        for(uint8_t axis = 0; axis < NUM_AXIS; ++axis)
                        {
                            sampled[axis] = forcesCondition[axis];
                        }
                    break;

                    default:
                        continue;
                }
                effectBlock.sampleTick = elapsedTime;
                effectBlock.sampleValid = true;
                for (uint8_t axis = 0; axis < NUM_AXIS; ++axis) {
                    effectBlock.sampledForces[axis] = sampled[axis];
                    forcesSum[axis] += sampled[axis] * effectGain / float(USB_MAX_EFFECT_GAIN);
                }
            }
        }

        // compute device gain rescaled forces
        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            forcesSum[i] *= ffbReportHandler.deviceGain / float(USB_MAX_DEVICE_GAIN);
            forces[i] = ffbReportHandler.deviceState == FFBReportHandler::DEVICE_STATE_DISABLED ? 0 :
                clamp(forcesSum[i], float(-USB_MAX_MAGNITUDE), float(USB_MAX_MAGNITUDE));
        }
    }

    float FFBForceCalculator::get_base_magnitude(const EffectBlock& effectBlock, uint8_t effectType) const
    {
        switch (effectType)
        {
            case ET_CONSTANT:
                return fabsf(float(effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].constantData.magnitude));
            case ET_RAMP:
            {
                const SetRampForceReportData& ramp = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].rampData;
                return fmaxf(fabsf(float(ramp.rampStart)), fabsf(float(ramp.rampEnd)));
            }
            default:
                return effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].periodicData.magnitude;
        }
    }

    float FFBForceCalculator::get_envelope(const SetEnvelopeReportData& envelopeData, uint32_t elapsedTime, uint16_t duration, float baseMagnitude) const
    {
        const uint16_t attackLevel = envelopeData.attackLevel;
        const uint16_t fadeLevel = envelopeData.fadeLevel;
        const uint16_t attackTime = envelopeData.attackTime;
        const uint16_t fadeTime = envelopeData.fadeTime;

        if(attackTime > 0 && elapsedTime < attackTime)
        {
            const float t = (float)elapsedTime / attackTime;
            return attackLevel + (baseMagnitude - attackLevel) * t;
        }

        if(USB_DURATION_INFINITE != duration && fadeTime > 0 && elapsedTime > (duration > fadeTime ? duration - fadeTime : 0))
        {
            const float t = (float)(elapsedTime - (duration > fadeTime ? duration - fadeTime : 0)) / fadeTime;
            return baseMagnitude + (fadeLevel - baseMagnitude) * t;
        }

        return baseMagnitude;
    }

}
