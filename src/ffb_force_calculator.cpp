#include "ffb_force_calculator.h"
#include "math_utils.h"

namespace SunFFB
{
    float FFBForceCalculator::constant_force_calculator(const EffectBlock& effectBlock) const
    {
        return effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].constantData.magnitude;
    }

    float FFBForceCalculator::ramp_force_calculator(const EffectBlock& effectBlock, float elapsedTime) const
    {
        const SetRampForceReportData& rampData = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].rampData;
        return rampData.rampStart + (rampData.rampEnd - rampData.rampStart) * elapsedTime / float(effectBlock.effectData.duration);
    }

    float FFBForceCalculator::periodic_force_calculator(uint8_t effectType, const EffectBlock& effectBlock, uint32_t elapsedTime) const
    {
        const SetPeriodicReportData& periodicData = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].periodicData;

        const int16_t offset = periodicData.offset;
        const int16_t magnitude = periodicData.magnitude;
        const uint16_t phase = periodicData.phase;
        const uint16_t period = periodicData.period > 0 ? periodicData.period : 1;

        const float phaseNormalized = phase / (float)(USB_MAX_PHASE);
        const uint16_t timeRemaind = uint32_t(phaseNormalized * period + elapsedTime) % period;

        float force = 0.f;
        switch (effectType)
        {
            case ET_SQUARE:
            {
                if(timeRemaind >= (period >> 1))
                    force = -magnitude;
                else
                    force = magnitude;
                force += offset;
            }
            break;

            case ET_SINE:
            {
                #ifndef USE_FAST_MATH
                const float angle = 2 * (float)M_PI * (elapsedTime / float(period) + phaseNormalized);
                force = sinf(angle) * magnitude;
                #else
                const float angle = normalize_angle(2 * (float)M_PI * (elapsedTime / float(period) + phaseNormalized));
                force = _sinf(angle) * magnitude;
                #endif
                force += offset;
            }
            break;

            case ET_TRIANGLE:
            {
                const float slope = 4.f * magnitude / float(period);
                const uint32_t phaseOffset = period >> 2;
                const uint32_t offsetRemaind = (timeRemaind + phaseOffset) % period;

                if(offsetRemaind >= (period >> 1))
                    force = slope * (period - offsetRemaind);
                else
                    force = slope * offsetRemaind;
                
                force -= magnitude;
                force += offset;
            }
            break;

            case ET_SAWTOOTH_DOWN:
            case ET_SAWTOOTH_UP:
            {
                const float slope = (float)magnitude / float(period);
                if(ET_SAWTOOTH_DOWN == effectType)
                    force = slope * (period - timeRemaind);
                else
                    force = slope * timeRemaind;
                
                force += offset;
            }
            break;
            
            default:
                return 0.f;
        }

        return force;
    }

    float FFBForceCalculator::apply_condition(const SetConditionReportData& conditionData, float metric) const
    {
        const int16_t cpOffset = conditionData.cpOffset;
        const int16_t postiveCoeff = conditionData.positiveCoefficient;
        const int16_t negativeCoeff = conditionData.negativeCoefficient;
        const int16_t postiveSaturation = conditionData.positiveSaturation;
        const int16_t negativeSaturation = -conditionData.negativeSaturation;
        const uint16_t deadBand = conditionData.deadBand;

        float force = 0.f;

        constexpr float invRange =  1.f / USB_AXIS_MAX_ABSOLUTE;
        if(metric < (cpOffset - deadBand) * invRange)
        {
            force = (metric - (cpOffset - deadBand) * invRange) * negativeCoeff;
            force = force < negativeSaturation ? negativeSaturation : force;
        }
        else if(metric > (cpOffset + deadBand) * invRange)
        {
            force = (metric - (cpOffset + deadBand) * invRange) * postiveCoeff;
            force = force > postiveSaturation ? postiveSaturation : force;
        }

        return -force;
    }

    void FFBForceCalculator::condition_force_calculator(const EffectBlock& effectBlock, const float metrics[NUM_AXIS], const int32_t maxMetrics[NUM_AXIS], float forces[NUM_AXIS]) const
    {
        const uint8_t axisEnable = effectBlock.effectData.axisEnable;
        const uint8_t conditionBlockFlags = effectBlock.conditionBlockFlags;

        if(axisEnable & DIRECTION_ENABLE)
        {
            if(conditionBlockFlags > 1)
            {
                #pragma unroll
                for(uint8_t i = 0; i < NUM_AXIS; ++i)
                {
                    const SetConditionReportData& conditionData = effectBlock.typeSpecificData[i].conditionData;
                    forces[i] = apply_condition(conditionData, normalize_range(metrics[i], maxMetrics[i]));
                }
            }
            else
            {
                const SetConditionReportData& conditionData = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].conditionData;
                const float* directionUnitVector = effectBlock.directionUnitVector;

                float metric = 0.f;
                #pragma unroll
                for(uint8_t i = 0; i < NUM_AXIS; ++i)
                    metric += metrics[i] * directionUnitVector[i];
                
                const float force = apply_condition(conditionData, normalize_range(metric, maxMetrics[0]));

                #pragma unroll
                for(uint8_t i = 0; i < NUM_AXIS; ++i)
                    forces[i] = force * directionUnitVector[i];
            }
        }
        else
        {
            const float* directionUnitVector = effectBlock.directionUnitVector;
            #pragma unroll
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
            {
                forces[i] = 0.f;
                if((axisEnable >> i) & 0x01)
                {
                    const SetConditionReportData& conditionData = effectBlock.typeSpecificData[i].conditionData;
                    forces[i] = apply_condition(conditionData, normalize_range(metrics[i], maxMetrics[i])) * directionUnitVector[i];
                }
            }
        }
    }

    void FFBForceCalculator::force_calculator(const FFBReportHandler& ffbReportHandler, const FFBDeviceInput& ffbDeviceInput, int32_t forces[NUM_AXIS]) const
    {
        if(ffbReportHandler.devicePaused || !(ffbReportHandler.get_pid_state_report_data()->status & 0x02))
        {
            #pragma unroll
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
                forces[i] = 0;
            return;
        }

        const EffectBlock* effectBlocks = ffbReportHandler.get_all_effect_blocks();

        float forcesSum[NUM_AXIS] = {0};
        const uint32_t currentTime = millis();

        for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            const EffectBlock& effectBlock = effectBlocks[i];

            if(is_effect_playing(effectBlock, ffbDeviceInput.inputData.buttons, currentTime))
            {
                const uint8_t effectType = effectBlock.effectData.effectType;
                const uint16_t duration = effectBlock.effectData.duration;
                const uint32_t elapsedTime = currentTime - effectBlock.startTime;
                const uint8_t effectGain = effectBlock.effectData.gain;

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
                        if(effectBlock.envelopParameter)
                        {
                            const SetEnvelopeReportData& envelopeData = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_2].envelopeData;
                            force *= get_envelope(envelopeData, elapsedTime, duration);
                        }

                        force *= effectGain / float(USB_MAX_EFFECT_GAIN);

                        #pragma unroll
                        for(uint8_t i = 0; i < NUM_AXIS; ++i)
                        {
                            // TODO: test
                            if((effectBlock.effectData.axisEnable & DIRECTION_ENABLE) || ((effectBlock.effectData.axisEnable >> i) & 0x01))
                                forcesSum[i] += force * effectBlock.directionUnitVector[i];
                        }
                    }
                    break;

                    case ET_SPRING:
                    case ET_FRICTION:
                    case ET_DAMPER:
                    case ET_INERTIA:
                        #pragma unroll
                        for(uint8_t i = 0; i < NUM_AXIS; ++i)
                        {
                            forcesCondition[i] *= effectGain / float(USB_MAX_EFFECT_GAIN);
                            forcesSum[i] += forcesCondition[i];
                        }
                    break;

                    default:
                        continue;
                }
            }
        }

        // compute device gain rescaled forces
        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            forcesSum[i] *= ffbReportHandler.deviceGain / float(USB_MAX_DEVICE_GAIN);
            forces[i] = clamp(forcesSum[i], float(-USB_MAX_MAGNITUDE), float(USB_MAX_MAGNITUDE));
        }
    }

    float FFBForceCalculator::get_envelope(const SetEnvelopeReportData& envelopeData, uint32_t elapsedTime, uint16_t duration) const
    {
        const uint16_t attackLevel = envelopeData.attackLevel;
        const uint16_t fadeLevel = envelopeData.fadeLevel;
        const uint16_t attackTime = envelopeData.attackTime;
        const uint16_t fadeTime = envelopeData.fadeTime;

        float envelope = USB_MAX_MAGNITUDE;

        if(attackTime > 0 && elapsedTime < attackTime)
        {
            const float height = USB_MAX_MAGNITUDE - attackLevel;
            const float slope = height / (float)attackTime;
            envelope = slope * elapsedTime + attackLevel;

            return envelope / float(USB_MAX_MAGNITUDE);
        }

        if(USB_DURATION_INFINITE == duration) return 1.f;

        if(fadeTime > 0 && elapsedTime > (duration - fadeTime))
        {
            const float height = USB_MAX_MAGNITUDE - fadeLevel;
            const float slope = height / (float)fadeTime;
            envelope = slope * (duration - elapsedTime) + fadeLevel;

            return envelope / float(USB_MAX_MAGNITUDE);
        }

        return 1.f;
    }

    bool FFBForceCalculator::is_trigger_playing(EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime) const
    {
        const uint32_t elapsedTime = currentTime - effectBlock.startTime;
        const uint8_t buttonIdx = effectBlock.effectData.triggerButton - 1;
        const bool buttonPressed = ((triggerButtonState >> buttonIdx) & 0x01);

        if(!buttonPressed)
        {
            effectBlock.triggerButtonLatch = false;
            return false;
        }
        else
        {
            if(!effectBlock.triggerButtonLatch)
            {
                // TODO: need check
                effectBlock.startTime = currentTime + effectBlock.effectData.startDelay;
                effectBlock.triggerButtonLatch = true;
                return true;
            }
            else
            {
                if(elapsedTime < effectBlock.effectData.duration)
                    return true;
                
                if(USB_DURATION_INFINITE == effectBlock.effectData.triggerRepeatInterval)
                    return false;
                
                if(elapsedTime < (effectBlock.effectData.duration + effectBlock.effectData.triggerRepeatInterval))
                    return false;
                
                effectBlock.startTime = currentTime;
                return true;
            }
        }

        return true;
    }

    bool FFBForceCalculator::is_effect_playing(const EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime) const
    {
        if(!(effectBlock.state & EFFECT_STATE_PLAYING))
            return false;

        if(USB_NO_TRIGGER_BUTTON != effectBlock.effectData.triggerButton)
            return is_trigger_playing(const_cast<EffectBlock&>(effectBlock), triggerButtonState, currentTime);
        
        if(currentTime < effectBlock.startTime)
            return false;
        
        const uint32_t elapsedTime = currentTime - effectBlock.startTime;
        if((USB_DURATION_INFINITE != effectBlock.effectData.duration) && (elapsedTime >= effectBlock.effectData.duration))
            return false;
        
        return true; 
    }
}