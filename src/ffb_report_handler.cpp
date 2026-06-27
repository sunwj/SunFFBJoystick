#include "ffb_report_handler.h"
#include "math_utils.h"

// #define SERIAL_PRINT

namespace SunFFB
{
    FFBReportHandler::FFBReportHandler()
    {
        init();
    }

    void FFBReportHandler::init()
    {
        devicePaused = 0;
        pauseTime = 0;
        free_all_effects();
    }

    void FFBReportHandler::create_new_effect(const CreateNewEffectReportData* data)
    {
        blockLoadData.effectBlockIndex = get_next_free_effect_block_index();

        if(0 == blockLoadData.effectBlockIndex)
            blockLoadData.blockLoadStatus = 2;          // full
        else
        {
            blockLoadData.blockLoadStatus = 1;          // success

            volatile EffectBlock* effectBlock = get_effect_block(blockLoadData.effectBlockIndex);

            memset((void*)effectBlock, 0, sizeof(EffectBlock));
            effectBlock->state = EFFECT_STATE_ALLOCATED;
            effectBlock->effectData.effectType = data->effectType;

            blockLoadData.ramPoolAvailable -= sizeof(EffectBlock);
        }

        #ifdef SERIAL_PRINT
        Serial.printf("Create new effect: %d\n", blockLoadData.blockLoadStatus);
        #endif
    }

    uint8_t FFBReportHandler::get_next_free_effect_block_index()
    {
        // for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
        // {
        //     if(EFFECT_STATE_FREE == effectBlocks[i].state)
        //         return i + 1;
        // }

        // return 0;

        uint8_t idx = 0;
        for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            nextEffectIdx = MAX_EFFECTS == nextEffectIdx ? 0 : nextEffectIdx;
            if(EFFECT_STATE_FREE == effectBlocks[nextEffectIdx].state)
            {
                idx = ++nextEffectIdx;
                return idx;
            }
            ++nextEffectIdx;
        }

        return idx;
    }

    void FFBReportHandler::free_effect(uint8_t idx)
    {
        volatile EffectBlock* effectBlock = get_effect_block(idx);
        if(nullptr == effectBlock) return;

        if (effectBlock->state != EFFECT_STATE_FREE)
        {
            effectBlock->state = EFFECT_STATE_FREE;
            blockLoadData.ramPoolAvailable += sizeof(EffectBlock);
        }
        nextEffectIdx = idx - 1;
    }

    void FFBReportHandler::free_all_effects()
    {
        nextEffectIdx = 0;
        memset((void*)&effectBlocks, 0, sizeof(effectBlocks));
        blockLoadData.ramPoolAvailable = sizeof(effectBlocks);
    }

    EffectBlock* FFBReportHandler::get_effect_block(uint8_t idx)
    {
        if(idx > 0 && idx <= MAX_EFFECTS)
            return (EffectBlock*)&effectBlocks[idx - 1];
        
        return nullptr;
    }

    void FFBReportHandler::start_effect(volatile EffectBlock* effectBlock)
    {
        effectBlock->state |= EFFECT_STATE_PLAYING;
        effectBlock->startTime = millis() + effectBlock->effectData.startDelay;
        if(effectBlock->effectData.triggerButton != USB_NO_TRIGGER_BUTTON)
        {
            // TODO: need check
            effectBlock->startTime = 0;
            effectBlock->triggerButtonLatch = false;
        }
    }

    void FFBReportHandler::stop_all_effects()
    {
        for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
            stop_effect(&effectBlocks[i]);
    }

    const PoolReportData* FFBReportHandler::get_pool_report_data()
    {
        poolData.ramPoolSize = sizeof(effectBlocks);
        poolData.maxSimultaneousEffects = MAX_EFFECTS;
        poolData.managedPool = 3;

        return (const PoolReportData*)&poolData;
    }

    void FFBReportHandler::set_effect(const SetEffectReportData* data)
    {
        volatile EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        volatile SetEffectReportData* effectData = &effectBlock->effectData;
        memcpy((void*)effectData, data, sizeof(SetEffectReportData));

        effectBlock->originalDuration = effectData->duration;

        const uint8_t enableAxis = data->axisEnable;
        if(enableAxis & DIRECTION_ENABLE)
        {
            #if NUM_AXIS == 1
            effectBlock->directionUnitVector[0] = 1.f;
            #endif

            #if NUM_AXIS == 2
            const float theta = data->directions[0] * USB_NORMALIZATION_RAD;
            #ifndef USE_FAST_MATH
            effectBlock->directionUnitVector[0] = -sinf(theta);
            effectBlock->directionUnitVector[1] = cosf(theta);
            #else
            effectBlock->directionUnitVector[0] = -_sinf(theta);
            effectBlock->directionUnitVector[1] = _cosf(theta);
            #endif
            #endif

            #if NUM_AXIS == 3
            const float theta = data->directions[0] * USB_NORMALIZATION_RAD;
            const float phi = data->directions[1] * USB_NORMALIZATION_RAD;
            #ifndef USE_FAST_MATH
            const float cosPhi = cosf(phi);
            effectBlock->directionUnitVector[0] = -sinf(phi);
            effectBlock->directionUnitVector[1] = -cosPhi * sinf(theta);
            effectBlock->directionUnitVector[2] = -cosPhi * cosf(theta);
            #else
            const float cosPhi = _cosf(phi);
            effectBlock->directionUnitVector[0] = -_sinf(phi);
            effectBlock->directionUnitVector[1] = -cosPhi * _sinf(theta);
            effectBlock->directionUnitVector[2] = -cosPhi * _cosf(theta);
            #endif
            #endif
        }
        else
        {
            #if NUM_AXIS == 1
            effectBlock->directionUnitVector[0] = 1.f;
            #endif

            #if NUM_AXIS == 2
            const float x = float(data->directions[1]);
            const float y = float(data->directions[0]);
            float invLen = 1.f / sqrtf(x * x + y * y + 1e-6f);
            effectBlock->directionUnitVector[0] = -x * invLen;
            effectBlock->directionUnitVector[1] = -y * invLen;
            #endif

            #if NUM_AXIS == 3
            const float x = float(data->directions[2]);
            const float y = float(data->directions[1]);
            const float z = float(data->directions[0]);
            float invLen = 1.f / sqrtf(x * x + y * y + z * z + 1e-6f);
            effectBlock->directionUnitVector[0] = -x * invLen;
            effectBlock->directionUnitVector[1] = -y * invLen;
            effectBlock->directionUnitVector[2] = -z * invLen;
            #endif
        }

        #ifdef SERIAL_PRINT
        #if NUM_AXIS == 1
        Serial.printf("Set effect: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d \n", effectData->effectBlockIndex, effectData->effectType, effectData->duration, \
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain, effectData->triggerButton, effectData->axisEnable, effectData->directions[0], effectData->startDelay);
        #elif NUM_AXIS == 2
        Serial.printf("Set effect: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d \n", effectData->effectBlockIndex, effectData->effectType, effectData->duration, \
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain, effectData->triggerButton, effectData->axisEnable, effectData->directions[0], effectData->directions[1], effectData->startDelay);
        #endif
        #endif
    }

    void FFBReportHandler::set_envelope(const SetEnvelopeReportData* data)
    {
        volatile EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        volatile SetEnvelopeReportData* envelopData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_2].envelopeData);
        memcpy((void*)envelopData, data, sizeof(SetEnvelopeReportData));
        effectBlock->envelopParameter = true;

        #ifdef SERIAL_PRINT
        Serial.printf("Set envelope. %d, %d, %d, %d, %d \n", envelopData->effectBlockIndex, envelopData->attackLevel, envelopData->fadeLevel, envelopData->attackTime, envelopData->fadeTime);
        #endif
    }

    void FFBReportHandler::set_condition(const SetConditionReportData* data)
    {
        const uint8_t parameterBlockOffset = data->parameterBlockOffset & 0x0F;
        if(parameterBlockOffset > (NUM_AXIS - 1)) return;

        volatile EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        volatile SetConditionReportData* conditionData = &(effectBlock->typeSpecificData[parameterBlockOffset].conditionData);
        memcpy((void*)conditionData, data, sizeof(SetConditionReportData));

        effectBlock->conditionBlockFlags |= (0x01 << parameterBlockOffset);

        #ifdef SERIAL_PRINT
        Serial.printf("Set condition. %d, %d, %d, %d, %d, %d, %d, %d \n", conditionData->effectBlockIndex, conditionData->parameterBlockOffset, conditionData->cpOffset, \
            conditionData->positiveCoefficient, conditionData->negativeCoefficient, conditionData->positiveSaturation, conditionData->negativeSaturation, conditionData->deadBand);
        #endif
    }

    void FFBReportHandler::set_periodic(const SetPeriodicReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetPeriodicReportData* periodicData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].periodicData);
        memcpy((void*)periodicData, data, sizeof(SetPeriodicReportData));

        #ifdef SERIAL_PRINT
        Serial.printf("Set periodic. %d, %d, %d, %d, %d \n", periodicData->effectBlockIndex, periodicData->magnitude, periodicData->offset, periodicData->phase, periodicData->period);
        #endif
    }

    void FFBReportHandler::set_constant_force(const SetConstantForceReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetConstantForceReportData* constantData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].constantData);
        memcpy(constantData, data, sizeof(SetConstantForceReportData));

        #ifdef SERIAL_PRINT
        Serial.printf("Set constant. %d, %d \n", constantData->effectBlockIndex, constantData->magnitude);
        #endif
    }

    void FFBReportHandler::set_ramp_force(const SetRampForceReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetRampForceReportData* rampData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].rampData);
        memcpy(rampData, data, sizeof(SetRampForceReportData));

        #ifdef SERIAL_PRINT
        Serial.printf("Set ramp. %d, %d, %d \n", rampData->effectBlockIndex, rampData->rampStart, rampData->rampEnd);
        #endif
    }

    void FFBReportHandler::set_device_gain(const DeviceGainReportData* data)
    {
        deviceGain = data->gain;

        #ifdef SERIAL_PRINT
        Serial.printf("device gain. %d \n", deviceGain);
        #endif
    }

    void FFBReportHandler::set_device_control(const DeviceControlReportData* data)
    {
        // TODO: set_device_control() updates PID status bit 1 for enable/disable actuators, but force_calculator() only checks devicePaused
        switch(data->state)
        {
            case 1:                 // enable actuators
                pidStates.status |= 0x02;
                deviceState = DEVICE_STATE_ACTIVE;
            break;

            case 2:                 // disable actuators
                pidStates.status &= ~(0x02);
                deviceState = DEVICE_STATE_DISABLED;
            break;

            case 3:                 // stop all effects
                stop_all_effects();
            break;

            case 4:                 // reset
                free_all_effects();
                devicePaused = false;
                pauseTime = 0;
                deviceGain = USB_MAX_DEVICE_GAIN;
                pidStates.status = 0x1E;
                pidStates.effectBlockIndex = 0;
                deviceState = DEVICE_STATE_INIT;
            break;

            case 5:                 // pause
                devicePaused = true;
                pidStates.status |= 1;
                pauseTime = millis();
                deviceState = DEVICE_STATE_PAUSED;
            break;

            case 6:                 // continue
            {
                devicePaused = false;
                pidStates.status &= ~(0x01);
                deviceState = DEVICE_STATE_ACTIVE;

                const uint32_t pauseLength = millis() - pauseTime;
                for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
                {
                    if(effectBlocks[i].state & EFFECT_STATE_PLAYING)
                    {
                        if(pauseTime < effectBlocks[i].startTime) continue;
                        effectBlocks[i].startTime += pauseLength;
                    }
                }
            }
            break;
        }

        #ifdef SERIAL_PRINT
        Serial.printf("Device control. %d \n", data->state);
        #endif
    }

    void FFBReportHandler::set_effect_operation(const EffectOperationReportData* data)
    {
        volatile EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock) return;

        switch (data->effectOperation)
        {
            case 1:                 // start
            {
                effectBlock->effectData.duration = effectBlock->originalDuration;
                if(0xFF == data->loopCount)
                    effectBlock->effectData.duration = USB_DURATION_INFINITE;
                else if(data->loopCount > 0)
                    effectBlock->effectData.duration = data->loopCount * effectBlock->originalDuration;

                start_effect(effectBlock);
            }
            break;

            case 2:                 // start solo
            {
                stop_all_effects();

                effectBlock->effectData.duration = effectBlock->originalDuration;
                if(0xFF == data->loopCount)
                    effectBlock->effectData.duration = USB_DURATION_INFINITE;
                else if(data->loopCount > 0)
                    effectBlock->effectData.duration = data->loopCount * effectBlock->originalDuration;

                start_effect(effectBlock);
            }
            break;

            case 3:                 // stop
            {
                stop_effect(effectBlock);
            }
            break;
        }

        #ifdef SERIAL_PRINT
        Serial.printf("Effect operation. %d, %d, %d \n", data->effectBlockIndex, data->effectOperation, data->loopCount);
        #endif
    }

    void FFBReportHandler::set_effect_block_free(const BlockFreeReportData* data)
    {
        if(0xFF == data->effectBlockIndex)
            free_all_effects();
        else
            free_effect(data->effectBlockIndex);

        #ifdef SERIAL_PRINT
        Serial.printf("Block free. %d \n", data->effectBlockIndex);
        #endif
    }
} // namespace SunFFB