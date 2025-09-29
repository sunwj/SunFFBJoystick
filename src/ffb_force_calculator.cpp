#include <algorithm>
#include "ffb_force_calculator.h"

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

        int16_t offset = periodicData.offset;
        int16_t magnitude = periodicData.magnitude;
        uint16_t phase = periodicData.phase;
        uint16_t period = periodicData.period > 0 ? periodicData.period : 1;

        float phaseNormalized = phase / (float)(USB_MAX_PHASE);
        uint16_t timeRemaind = uint32_t(phaseNormalized * period + elapsedTime) % period;

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
                float angle = 2 * (float)M_PI * (elapsedTime / float(period) + phaseNormalized);
                force = sinf(angle) * magnitude;
                force += offset;
            }
            break;

            case ET_TRIANGLE:
            {
                float slope = 4.f * magnitude / float(period);
                const uint32_t phaseOffset = period >> 2;
                uint32_t offsetRemaind = (timeRemaind + phaseOffset) % period;

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
                float slope = (float)magnitude / float(period);
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

    float FFBForceCalculator::apply_condition(const SetConditionReportData& conditionData, int32_t metric) const
    {
        int16_t cpOffset = conditionData.cpOffset;
        int16_t postiveCoeff = conditionData.positiveCoefficient;
        int16_t negativeCoeff = conditionData.negativeCoefficient;
        int16_t postiveSaturation = conditionData.positiveSaturation;
        int16_t negativeSaturation = -conditionData.negativeSaturation;
        uint8_t deadBand = conditionData.deadBand;

        float force = 0.f;

        if(metric < (cpOffset - deadBand))
        {
            force = (metric - (cpOffset - deadBand)) * negativeCoeff / (float)USB_AXIS_MAX_ABSOLUTE;
            force = force < negativeSaturation ? negativeSaturation : force;
        }
        else if(metric > (cpOffset + deadBand))
        {
            force = (metric - (cpOffset + deadBand)) * postiveCoeff / (float)USB_AXIS_MAX_ABSOLUTE;
            force = force > postiveSaturation ? postiveSaturation : force;
        }

        return -force;
    }

    void FFBForceCalculator::condition_force_calculator(const EffectBlock& effectBlock, const int32_t metrics[NUM_AXIS], float forces[NUM_AXIS]) const
    {
        uint8_t axisEnable = effectBlock.effectData.axisEnable;
        uint8_t conditionBlockCount = effectBlock.conditionBlockCount;

        if((axisEnable & DIRECTION_ENABLE) && (1 == conditionBlockCount))
        {
            const SetConditionReportData& conditionData = effectBlock.typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].conditionData;
            const float* directionUnitVector = effectBlock.directionUnitVector;

            float metric = 0.f;
            #pragma unroll
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
                metric += metrics[i] * directionUnitVector[i];
            
            float force = apply_condition(conditionData, metric);

            #pragma unroll
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
                forces[i] = force * directionUnitVector[i];
            
            return;
        }

        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            forces[i] = 0.f;
            if(((axisEnable & DIRECTION_ENABLE) && (conditionBlockCount > 1)) || ((axisEnable >> i) & 0x01))
            {
                const SetConditionReportData& conditionData = effectBlock.typeSpecificData[i].conditionData;
                forces[i] = apply_condition(conditionData, metrics[i]);
            }
        }
    }

    void FFBForceCalculator::force_calculator(const FFBReportHandler& ffbReportHandler, const FFBDeviceInput& ffbDeviceInput, int32_t forces[NUM_AXIS]) const
    {
        if(ffbReportHandler.devicePaused)
        {
            #pragma unroll
            for(uint8_t i = 0; i < NUM_AXIS; ++i)
                forces[i] = 0;
            return;
        }

        const EffectBlock* effectBlocks = ffbReportHandler.get_all_effect_blocks();

        float forcesSum[NUM_AXIS] = {0};
        uint32_t currentTime = millis();

        for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            const EffectBlock& effectBlock = effectBlocks[i];

            if(is_effect_playing(effectBlock, ffbDeviceInput.inputData.buttons, currentTime))
            {
                uint8_t effectType = effectBlock.effectData.effectType;
                uint16_t duration = effectBlock.effectData.duration;
                uint32_t elapsedTime = currentTime - effectBlock.startTime;
                uint8_t effectGain = effectBlock.effectData.gain;

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
                        condition_force_calculator(effectBlock, ffbDeviceInput.get_position(), forcesCondition);
                    break;

                    case ET_FRICTION:
                    case ET_DAMPER:
                        condition_force_calculator(effectBlock, ffbDeviceInput.get_speed(), forcesCondition);
                    break;

                    case ET_INERTIA:
                        condition_force_calculator(effectBlock, ffbDeviceInput.get_acceleration(), forcesCondition);
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
                            if(effectBlock.effectData.axisEnable & DIRECTION_ENABLE)
                                forcesSum[i] += force * effectBlock.directionUnitVector[i];
                            else if((effectBlock.effectData.axisEnable >> i) & 0x01)
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
            forces[i] = std::clamp(forcesSum[i], float(-USB_MAX_MAGNITUDE), float(USB_MAX_MAGNITUDE));
        }
    }

    float FFBForceCalculator::get_envelope(const SetEnvelopeReportData& envelopeData, uint32_t elapsedTime, uint16_t duration) const
    {
        uint8_t attackLevel = envelopeData.attackLevel;
        uint8_t fadeLevel = envelopeData.fadeLevel;
        uint16_t attackTime = envelopeData.attackTime;
        uint16_t fadeTime = envelopeData.fadeTime;

        float envelope = USB_MAX_MAGNITUDE;

        if(elapsedTime < attackTime)
        {
            float height = USB_MAX_MAGNITUDE - attackLevel;
            float slope = height / (float)attackTime;
            envelope = slope * elapsedTime + attackLevel;

            return envelope / float(USB_MAX_MAGNITUDE);
        }

        if(USB_DURATION_INFINITE == duration) return 1.f;

        if(elapsedTime > (duration - fadeTime))
        {
            float height = USB_MAX_MAGNITUDE - fadeLevel;
            float slope = height / (float)fadeTime;
            envelope = slope * (duration - elapsedTime) + fadeLevel;

            return envelope / float(USB_MAX_MAGNITUDE);
        }

        return 1.f;
    }

    bool FFBForceCalculator::is_trigger_playing(EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime) const
    {
        uint32_t elapsedTime = currentTime - effectBlock.startTime;
        uint8_t buttonIdx = effectBlock.effectData.triggerButton - 1;
        bool buttonPressed = ((triggerButtonState >> buttonIdx) & 0x01);

        if(!buttonPressed)
        {
            effectBlock.triggerButtonLatch = false;
            return false;
        }
        else
        {
            if(!effectBlock.triggerButtonLatch)
            {
                effectBlock.startTime = currentTime;
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
        
        uint32_t elapsedTime = currentTime - effectBlock.startTime;
        if((USB_DURATION_INFINITE != effectBlock.effectData.duration) && (elapsedTime >= effectBlock.effectData.duration))
            return false;
        
        return true; 
    }
}