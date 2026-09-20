#include "ffb_report_handler.h"
#include <cstring>
#include "math_utils.h"
#include "ffb_hal.h"


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

            EffectBlock* effectBlock = get_effect_block(blockLoadData.effectBlockIndex);

            memset((void*)effectBlock, 0, sizeof(EffectBlock));
            effectBlock->state = EFFECT_STATE_ALLOCATED;
            effectBlock->effectData.effectType = data->effectType;

            blockLoadData.ramPoolAvailable -= sizeof(EffectBlock);
        }

        pidStateDirty = true;

        #ifdef SERIAL_PRINT
        _debug_printf("Create new effect: status=%d (1=success, 2=full)\n", blockLoadData.blockLoadStatus);
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
        EffectBlock* effectBlock = get_effect_block(idx);
        if(nullptr == effectBlock) return;

        if (effectBlock->state != EFFECT_STATE_FREE)
        {
            effectBlock->state = EFFECT_STATE_FREE;
            blockLoadData.ramPoolAvailable += sizeof(EffectBlock);
            update_pid_effect_index();
        }
        nextEffectIdx = idx - 1;
        pidStateDirty = true;
    }

    void FFBReportHandler::free_all_effects()
    {
        nextEffectIdx = 0;
        memset((void*)&effectBlocks, 0, sizeof(effectBlocks));
        blockLoadData.ramPoolAvailable = sizeof(effectBlocks);
        pidStates.effectBlockIndex = 0;
        pidStateDirty = true;
    }

    EffectBlock* FFBReportHandler::get_effect_block(uint8_t idx) const
    {
        if(idx > 0 && idx <= MAX_EFFECTS)
            return (EffectBlock*)&effectBlocks[idx - 1];
        
        return nullptr;
    }

    void FFBReportHandler::start_effect(EffectBlock* effectBlock)
    {
        // Some hosts (e.g. DirectInput-based) never send DeviceControl=Enable Actuators;
        // treat starting playback as implicit actuator enable when in INIT state.
        if(deviceState == DEVICE_STATE_INIT)
        {
            deviceState = DEVICE_STATE_ACTIVE;
            pidStates.status |= 0x02;
            pidStateDirty = true;
        }

        effectBlock->state |= EFFECT_STATE_PLAYING;
        effectBlock->startTime = _millis() + effectBlock->effectData.startDelay;
        update_pid_effect_index();
        pidStateDirty = true;
        if(effectBlock->effectData.triggerButton != USB_NO_TRIGGER_BUTTON)
        {
            effectBlock->startTime = 0;
            effectBlock->triggerButtonLatch = false;
        }
    }

    void FFBReportHandler::stop_effect(EffectBlock* effectBlock)
    {
        effectBlock->state &= ~EFFECT_STATE_PLAYING;
        update_pid_effect_index();
        pidStateDirty = true;
    }

    void FFBReportHandler::stop_all_effects()
    {
        for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
            effectBlocks[i].state &= ~EFFECT_STATE_PLAYING;
        pidStates.effectBlockIndex = 0;
        pidStateDirty = true;
    }

    const PoolReportData* FFBReportHandler::get_pool_report_data()
    {
        poolData.ramPoolSize = sizeof(effectBlocks);
        poolData.maxSimultaneousEffects = MAX_EFFECTS;
        poolData.managedPool = 1;

        return (const PoolReportData*)&poolData;
    }

    void FFBReportHandler::set_effect(const SetEffectReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetEffectReportData* effectData = &effectBlock->effectData;
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
            effectBlock->directionUnitVector[0] = -cosPhi * cosf(theta);
            effectBlock->directionUnitVector[1] = -cosPhi * sinf(theta);
            effectBlock->directionUnitVector[2] = -sinf(phi);
            #else
            const float cosPhi = _cosf(phi);
            effectBlock->directionUnitVector[0] = -cosPhi * _cosf(theta);
            effectBlock->directionUnitVector[1] = -cosPhi * _sinf(theta);
            effectBlock->directionUnitVector[2] = -_sinf(phi);
            #endif
            #endif
        }
        else
        {
            #if NUM_AXIS == 1
            effectBlock->directionUnitVector[0] = 1.f;
            #endif

            #if NUM_AXIS == 2
            const float x = float((int16_t)data->directions[0]);
            const float y = float((int16_t)data->directions[1]);
            float invLen = 1.f / sqrtf(x * x + y * y + 1e-6f);
            effectBlock->directionUnitVector[0] = -x * invLen;
            effectBlock->directionUnitVector[1] = -y * invLen;
            #endif

            #if NUM_AXIS == 3
            const float x = float((int16_t)data->directions[0]);
            const float y = float((int16_t)data->directions[1]);
            const float z = float((int16_t)data->directions[2]);
            float invLen = 1.f / sqrtf(x * x + y * y + z * z + 1e-6f);
            effectBlock->directionUnitVector[0] = -x * invLen;
            effectBlock->directionUnitVector[1] = -y * invLen;
            effectBlock->directionUnitVector[2] = -z * invLen;
            #endif
        }

        #ifdef SERIAL_PRINT
        #if NUM_AXIS == 1
        _debug_printf("Set effect: idx=%d type=%d duration=%d repeat=%d samplePeriod=%d gain=%d trigBtn=%d axisEnable=0x%02x dir0=%d startDelay=%d\n", effectData->effectBlockIndex, effectData->effectType, effectData->duration, \
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain, effectData->triggerButton, effectData->axisEnable, effectData->directions[0], effectData->startDelay);
        #elif NUM_AXIS == 2
        _debug_printf("Set effect: idx=%d type=%d duration=%d repeat=%d samplePeriod=%d gain=%d trigBtn=%d axisEnable=0x%02x dir0=%d dir1=%d startDelay=%d\n", effectData->effectBlockIndex, effectData->effectType, effectData->duration, \
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain, effectData->triggerButton, effectData->axisEnable, effectData->directions[0], effectData->directions[1], effectData->startDelay);
        #elif NUM_AXIS == 3
        _debug_printf("Set effect: idx=%d type=%d duration=%d repeat=%d samplePeriod=%d gain=%d trigBtn=%d axisEnable=0x%02x dir0=%d dir1=%d dir2=%d startDelay=%d\n", effectData->effectBlockIndex, effectData->effectType, effectData->duration, \
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain, effectData->triggerButton, effectData->axisEnable, effectData->directions[0], effectData->directions[1], effectData->directions[2], effectData->startDelay);
        #endif
        #endif
    }

    void FFBReportHandler::set_envelope(const SetEnvelopeReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        // Condition effects use every type-specific slot as a per-axis
        // condition block.  Slot 1 is the Y-axis block, so accepting an
        // envelope here would overwrite it and leave only X force active.
        const uint8_t effectType = effectBlock->effectData.effectType;
        if(effectType == ET_SPRING || effectType == ET_DAMPER ||
           effectType == ET_INERTIA || effectType == ET_FRICTION)
            return;

        SetEnvelopeReportData* envelopData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_2].envelopeData);
        memcpy((void*)envelopData, data, sizeof(SetEnvelopeReportData));
        effectBlock->envelopParameter = true;

        #ifdef SERIAL_PRINT
        _debug_printf("Set envelope: idx=%d attackLevel=%d fadeLevel=%d attackTime=%d fadeTime=%d\n", envelopData->effectBlockIndex, envelopData->attackLevel, envelopData->fadeLevel, envelopData->attackTime, envelopData->fadeTime);
        #endif
    }

    void FFBReportHandler::set_condition(const SetConditionReportData* data)
    {
        const uint8_t parameterBlockOffset = data->parameterBlockOffset & 0x0F;
        if(parameterBlockOffset > (NUM_AXIS - 1)) return;

        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetConditionReportData* conditionData = &(effectBlock->typeSpecificData[parameterBlockOffset].conditionData);
        memcpy((void*)conditionData, data, sizeof(SetConditionReportData));

        effectBlock->conditionBlockFlags |= (0x01 << parameterBlockOffset);

        #ifdef SERIAL_PRINT
        _debug_printf("Set condition: idx=%d block=%d cpOffset=%d posCoeff=%d negCoeff=%d posSat=%d negSat=%d deadBand=%d\n", conditionData->effectBlockIndex, conditionData->parameterBlockOffset, \
            conditionData->cpOffset, conditionData->positiveCoefficient, conditionData->negativeCoefficient, conditionData->positiveSaturation, conditionData->negativeSaturation, conditionData->deadBand);
        #endif
    }

    void FFBReportHandler::set_periodic(const SetPeriodicReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetPeriodicReportData* periodicData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].periodicData);
        memcpy((void*)periodicData, data, sizeof(SetPeriodicReportData));

        #ifdef SERIAL_PRINT
        _debug_printf("Set periodic: idx=%d magnitude=%d offset=%d phase=%d period=%d\n", periodicData->effectBlockIndex, periodicData->magnitude, periodicData->offset, periodicData->phase, periodicData->period);
        #endif
    }

    void FFBReportHandler::set_constant_force(const SetConstantForceReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetConstantForceReportData* constantData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].constantData);
        memcpy(constantData, data, sizeof(SetConstantForceReportData));

        #ifdef SERIAL_PRINT
        _debug_printf("Set constant: idx=%d magnitude=%d\n", constantData->effectBlockIndex, constantData->magnitude);
        #endif
    }

    void FFBReportHandler::set_ramp_force(const SetRampForceReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if(nullptr == effectBlock) return;

        SetRampForceReportData* rampData = &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].rampData);
        memcpy(rampData, data, sizeof(SetRampForceReportData));

        #ifdef SERIAL_PRINT
        _debug_printf("Set ramp: idx=%d rampStart=%d rampEnd=%d\n", rampData->effectBlockIndex, rampData->rampStart, rampData->rampEnd);
        #endif
    }

    void FFBReportHandler::set_device_gain(const DeviceGainReportData* data)
    {
        deviceGain = data->gain;
        pidStateDirty = true;

        #ifdef SERIAL_PRINT
        _debug_printf("Device gain: %d\n", deviceGain);
        #endif
    }

    void FFBReportHandler::set_device_control(const DeviceControlReportData* data)
    {
        pidStateDirty = true;
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
                deviceState = DEVICE_STATE_ACTIVE;
            break;

            case 5:                 // pause
                devicePaused = true;
                pidStates.status |= 1;
                pauseTime = _millis();
                deviceState = DEVICE_STATE_PAUSED;
            break;

            case 6:                 // continue
            {
                devicePaused = false;
                pidStates.status &= ~(0x01);
                deviceState = DEVICE_STATE_ACTIVE;

                const uint32_t pauseLength = _millis() - pauseTime;
                for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
                {
                    if(effectBlocks[i].state & EFFECT_STATE_PLAYING)
                    {
                        effectBlocks[i].startTime += pauseLength;
                    }
                }
            }
            break;
        }

        #ifdef SERIAL_PRINT
        _debug_printf("Device control: state=%d (1=enable,2=disable,3=stopAll,4=reset,5=pause,6=continue)\n", data->state);
        #endif
    }

    void FFBReportHandler::set_effect_operation(const EffectOperationReportData* data)
    {
        pidStateDirty = true;
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock) return;

        switch (data->effectOperation)
        {
            case 1:                 // start
            {
                effectBlock->effectData.duration = effectBlock->originalDuration;
                effectBlock->remainingLoops = data->loopCount == 0 ? 1 : data->loopCount;

                start_effect(effectBlock);
            }
            break;

            case 2:                 // start solo
            {
                stop_all_effects();

                effectBlock->effectData.duration = effectBlock->originalDuration;
                effectBlock->remainingLoops = data->loopCount == 0 ? 1 : data->loopCount;

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
        _debug_printf("Effect operation: idx=%d op=%d loopCount=%d (op: 1=start,2=startSolo,3=stop)\n", data->effectBlockIndex, data->effectOperation, data->loopCount);
        #endif
    }

    void FFBReportHandler::set_effect_block_free(const BlockFreeReportData* data)
    {
        if(0xFF == data->effectBlockIndex)
            free_all_effects();
        else
            free_effect(data->effectBlockIndex);

        pidStateDirty = true;

        #ifdef SERIAL_PRINT
        _debug_printf("Block free: idx=%d (255=all)\n", data->effectBlockIndex);
        #endif
    }

    void FFBReportHandler::update_pid_effect_index()
    {
        uint8_t idx = 0;
        for(uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            if(effectBlocks[i].state & EFFECT_STATE_PLAYING)
            {
                idx = i + 1;
                break;
            }
        }
        if(idx)
            pidStates.effectBlockIndex = (idx << 1) | 0x01;
        else
            pidStates.effectBlockIndex = 0;
    }

    bool FFBReportHandler::is_trigger_playing(EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime)
    {
        if (USB_NO_TRIGGER_BUTTON == effectBlock.effectData.triggerButton) return false;
        if (effectBlock.effectData.triggerButton == 0) return false;
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
                effectBlock.startTime = currentTime + effectBlock.effectData.startDelay;
                effectBlock.triggerButtonLatch = true;
                if ((int32_t)(effectBlock.startTime - currentTime) > 0) return false;
                return true;
            }
            else
            {
                const uint32_t elapsedTime = currentTime - effectBlock.startTime;

                if(elapsedTime < effectBlock.effectData.duration)
                    return true;

                if(USB_DURATION_INFINITE == effectBlock.effectData.triggerRepeatInterval)
                    return false;

                if(elapsedTime < (effectBlock.effectData.duration + effectBlock.effectData.triggerRepeatInterval))
                    return false;

                effectBlock.startTime = currentTime + effectBlock.effectData.startDelay;
                return true;
            }
        }

        return true;
    }

    bool FFBReportHandler::is_effect_playing(uint8_t effectBlockIndex, uint8_t triggerButtonState, uint32_t currentTime)
    {
        EffectBlock* effectBlock = get_effect_block(effectBlockIndex);
        if(nullptr == effectBlock) return false;
        return is_effect_playing(*effectBlock, triggerButtonState, currentTime);
    }

    bool FFBReportHandler::is_effect_playing(EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime)
    {
        EffectBlock& eb = effectBlock;

        if(!(eb.state & EFFECT_STATE_PLAYING))
            return false;

        if(USB_NO_TRIGGER_BUTTON != eb.effectData.triggerButton)
            return is_trigger_playing(eb, triggerButtonState, currentTime);

        if ((int32_t)(eb.startTime - currentTime) > 0)
            return false;

        const uint32_t elapsedTime = currentTime - eb.startTime;
        const uint16_t duration = eb.effectData.duration;
        if((USB_DURATION_INFINITE != duration) && (elapsedTime >= duration))
        {
            if(duration > 0)
            {
                const uint32_t completedLoops = elapsedTime / duration;
                if(eb.remainingLoops == 0xFF)
                {
                    eb.startTime += completedLoops * duration;
                    return true;
                }
                if(completedLoops < eb.remainingLoops)
                {
                    eb.remainingLoops -= completedLoops;
                    eb.startTime += completedLoops * duration;
                    return true;
                }
            }

            eb.remainingLoops = 0;
            eb.state &= ~EFFECT_STATE_PLAYING;
            update_pid_effect_index();
            pidStateDirty = true;
            return false;
        }

        return true;
    }
} // namespace SunFFB
