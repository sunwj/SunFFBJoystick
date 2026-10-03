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
        devicePaused = false;
        actuatorsEnabled = false;
        actuatorsInitialized = false;
        deviceState = DEVICE_STATE_INIT;
        deviceGain = USB_MAX_DEVICE_GAIN;
        pidStates.status = 0x1C;
        pauseTime = 0;
        free_all_effects();
    }

    void FFBReportHandler::create_new_effect(const CreateNewEffectReportData* data)
    {
        blockLoadData.effectBlockIndex = get_next_free_effect_block_index();

        if (0 == blockLoadData.effectBlockIndex)
            blockLoadData.blockLoadStatus = 2; // full
        else
        {
            blockLoadData.blockLoadStatus = 1; // success

            EffectBlock* effectBlock = get_effect_block(blockLoadData.effectBlockIndex);

            memset((void*)effectBlock, 0, sizeof(EffectBlock));
            effectBlock->state = EFFECT_STATE_ALLOCATED;
            effectBlock->effectData.effectType = data->effectType;

            blockLoadData.ramPoolAvailable -= sizeof(EffectBlock);
        }

        pidStateDirty = true;

#ifdef SERIAL_PRINT
        _debug_printf("Create new effect: status=%d (1=success, 2=full)\n",
                      blockLoadData.blockLoadStatus);
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

        for (uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            nextEffectIdx = MAX_EFFECTS == nextEffectIdx ? 0 : nextEffectIdx;
            if (EFFECT_STATE_FREE == effectBlocks[nextEffectIdx].state)
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
        if (nullptr == effectBlock)
            return;

        effectBlock->sampleValid = false;

        if (effectBlock->state != EFFECT_STATE_FREE)
        {
            stop_effect(effectBlock);
            effectBlock->state = EFFECT_STATE_FREE;
            blockLoadData.ramPoolAvailable += sizeof(EffectBlock);
        }

        nextEffectIdx = idx - 1;
        pidStateDirty = true;
    }

    void FFBReportHandler::free_all_effects()
    {
        nextEffectIdx = 0;
        stop_all_effects();
        memset((void*)&effectBlocks, 0, sizeof(effectBlocks));
        blockLoadData.ramPoolAvailable = sizeof(effectBlocks);
        pidStates.effectBlockIndex = 0;
        pidStateDirty = true;
    }

    EffectBlock* FFBReportHandler::get_effect_block(uint8_t idx) const
    {
        if (idx > 0 && idx <= MAX_EFFECTS)
            return (EffectBlock*)&effectBlocks[idx - 1];

        return nullptr;
    }

    void FFBReportHandler::start_effect(EffectBlock* effectBlock)
    {
        // Some hosts (e.g. DirectInput-based) never send DeviceControl=Enable Actuators;
        // treat starting playback as implicit actuator enable when in INIT state.
        if (deviceState == DEVICE_STATE_INIT)
        {
            actuatorsEnabled = true;
            actuatorsInitialized = true;
            update_device_state();
            pidStateDirty = true;
        }

        effectBlock->state |= EFFECT_STATE_PLAYING;
        effectBlock->startTime =
            (devicePaused ? pauseTime : _millis()) + effectBlock->effectData.startDelay;
        effectBlock->sampleValid = false;
        effectBlock->loopCount = effectBlock->remainingLoops;
        effectBlock->triggerRunning = false;
        effectBlock->triggerRepeatPending = false;
        publish_effect_state(*effectBlock,
                             effectBlock->effectData.triggerButton == USB_NO_TRIGGER_BUTTON &&
                                 effectBlock->effectData.startDelay == 0);
        if (effectBlock->effectData.triggerButton != USB_NO_TRIGGER_BUTTON)
        {
            effectBlock->startTime = 0;
            effectBlock->triggerButtonLatch = false;
        }
    }

    void FFBReportHandler::stop_effect(EffectBlock* block)
    {
        block->state &= ~EFFECT_STATE_PLAYING;
        block->triggerRunning = false;
        block->triggerRepeatPending = false;
        block->sampleValid = false;
        publish_effect_state(*block, false);
    }

    void FFBReportHandler::stop_all_effects()
    {
        for (uint8_t i = 0; i < MAX_EFFECTS; ++i)
            if (effectBlocks[i].state != EFFECT_STATE_FREE)
                stop_effect((EffectBlock*)&effectBlocks[i]);
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
        if (nullptr == effectBlock)
            return;

        effectBlock->sampleValid = false;

        SetEffectReportData* effectData = &effectBlock->effectData;
        memcpy((void*)effectData, data, sizeof(SetEffectReportData));

        effectBlock->originalDuration = effectData->duration;

// The descriptor declares angular coordinates; Direction Enable selects condition mode.
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
#ifdef SERIAL_PRINT
#if NUM_AXIS == 1
        _debug_printf(
            "Set effect: idx=%d type=%d duration=%d repeat=%d samplePeriod=%d gain=%d trigBtn=%d axisEnable=0x%02x dir0=%d startDelay=%d\n",
            effectData->effectBlockIndex, effectData->effectType, effectData->duration,
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain,
            effectData->triggerButton, effectData->axisEnable, effectData->directions[0],
            effectData->startDelay);
#elif NUM_AXIS == 2
        _debug_printf(
            "Set effect: idx=%d type=%d duration=%d repeat=%d samplePeriod=%d gain=%d trigBtn=%d axisEnable=0x%02x dir0=%d dir1=%d startDelay=%d\n",
            effectData->effectBlockIndex, effectData->effectType, effectData->duration,
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain,
            effectData->triggerButton, effectData->axisEnable, effectData->directions[0],
            effectData->directions[1], effectData->startDelay);
#elif NUM_AXIS == 3
        _debug_printf(
            "Set effect: idx=%d type=%d duration=%d repeat=%d samplePeriod=%d gain=%d trigBtn=%d axisEnable=0x%02x dir0=%d dir1=%d dir2=%d startDelay=%d\n",
            effectData->effectBlockIndex, effectData->effectType, effectData->duration,
            effectData->triggerRepeatInterval, effectData->samplePeriod, effectData->gain,
            effectData->triggerButton, effectData->axisEnable, effectData->directions[0],
            effectData->directions[1], effectData->directions[2], effectData->startDelay);
#endif
#endif
    }

    void FFBReportHandler::set_envelope(const SetEnvelopeReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock)
            return;

        effectBlock->sampleValid = false;

        // Envelopes do not apply to condition effects. Their parameter slots
        // remain independent from envelope storage.
        const uint8_t effectType = effectBlock->effectData.effectType;
        if (effectType == ET_SPRING || effectType == ET_DAMPER || effectType == ET_INERTIA ||
            effectType == ET_FRICTION)
            return;

        SetEnvelopeReportData* envelopData = &(effectBlock->envelopeData);
        memcpy((void*)envelopData, data, sizeof(SetEnvelopeReportData));
        effectBlock->envelopParameter = true;

#ifdef SERIAL_PRINT
        _debug_printf(
            "Set envelope: idx=%d attackLevel=%d fadeLevel=%d attackTime=%d fadeTime=%d\n",
            envelopData->effectBlockIndex, envelopData->attackLevel, envelopData->fadeLevel,
            envelopData->attackTime, envelopData->fadeTime);
#endif
    }

    void FFBReportHandler::set_condition(const SetConditionReportData* data)
    {
        const uint8_t parameterBlockOffset = data->parameterBlockOffset & 0x0F;
        if (parameterBlockOffset > (NUM_AXIS - 1))
            return;

        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock)
            return;

        effectBlock->sampleValid = false;

        SetConditionReportData* conditionData =
            &(effectBlock->typeSpecificData[parameterBlockOffset].conditionData);
        memcpy((void*)conditionData, data, sizeof(SetConditionReportData));

        effectBlock->conditionBlockFlags |= (0x01 << parameterBlockOffset);

#ifdef SERIAL_PRINT
        _debug_printf(
            "Set condition: idx=%d block=%d cpOffset=%d posCoeff=%d negCoeff=%d posSat=%d negSat=%d deadBand=%d\n",
            conditionData->effectBlockIndex, conditionData->parameterBlockOffset,
            conditionData->cpOffset, conditionData->positiveCoefficient,
            conditionData->negativeCoefficient, conditionData->positiveSaturation,
            conditionData->negativeSaturation, conditionData->deadBand);
#endif
    }

    void FFBReportHandler::set_periodic(const SetPeriodicReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock)
            return;

        effectBlock->sampleValid = false;

        SetPeriodicReportData* periodicData =
            &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].periodicData);
        memcpy((void*)periodicData, data, sizeof(SetPeriodicReportData));

#ifdef SERIAL_PRINT
        _debug_printf("Set periodic: idx=%d magnitude=%d offset=%d phase=%d period=%d\n",
                      periodicData->effectBlockIndex, periodicData->magnitude, periodicData->offset,
                      periodicData->phase, periodicData->period);
#endif
    }

    void FFBReportHandler::set_constant_force(const SetConstantForceReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock)
            return;

        effectBlock->sampleValid = false;

        SetConstantForceReportData* constantData =
            &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].constantData);
        memcpy(constantData, data, sizeof(SetConstantForceReportData));

#ifdef SERIAL_PRINT
        _debug_printf("Set constant: idx=%d magnitude=%d\n", constantData->effectBlockIndex,
                      constantData->magnitude);
#endif
    }

    void FFBReportHandler::set_ramp_force(const SetRampForceReportData* data)
    {
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock)
            return;

        effectBlock->sampleValid = false;

        SetRampForceReportData* rampData =
            &(effectBlock->typeSpecificData[TYPE_SPECIFIC_BLOCK_OFFSET_1].rampData);
        memcpy(rampData, data, sizeof(SetRampForceReportData));

#ifdef SERIAL_PRINT
        _debug_printf("Set ramp: idx=%d rampStart=%d rampEnd=%d\n", rampData->effectBlockIndex,
                      rampData->rampStart, rampData->rampEnd);
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

    void FFBReportHandler::update_device_state()
    {
        pidStates.status =
            (pidStates.status & ~0x03) | (devicePaused ? 0x01 : 0) | (actuatorsEnabled ? 0x02 : 0);
        deviceState =
            !actuatorsInitialized
                ? DEVICE_STATE_INIT
                : (!actuatorsEnabled ? DEVICE_STATE_DISABLED
                                     : (devicePaused ? DEVICE_STATE_PAUSED : DEVICE_STATE_ACTIVE));
    }

    void FFBReportHandler::set_device_control(const DeviceControlReportData* data)
    {
        pidStateDirty = true;

        switch (data->state)
        {
            case 1: // enable actuators
                actuatorsInitialized = true;
                actuatorsEnabled = true;
                update_device_state();
                break;

            case 2: // disable actuators
                actuatorsInitialized = true;
                actuatorsEnabled = false;
                update_device_state();
                break;

            case 3: // stop all effects
                stop_all_effects();
                break;

            case 4: // reset
                free_all_effects();
                devicePaused = false;
                pauseTime = 0;
                deviceGain = USB_MAX_DEVICE_GAIN;
                actuatorsInitialized = true;
                actuatorsEnabled = true;
                pidStates.status = 0x1E;
                pidStates.effectBlockIndex = 0;
                deviceState = DEVICE_STATE_ACTIVE;
                break;

            case 5: // pause
                if (!devicePaused)
                {
                    devicePaused = true;
                    pauseTime = _millis();
                    update_device_state();
                }

                break;

            case 6: // continue
            {
                if (!devicePaused)
                    break;
                devicePaused = false;
                update_device_state();

                const uint32_t pauseLength = _millis() - pauseTime;

                for (uint8_t i = 0; i < MAX_EFFECTS; ++i)
                {
                    if (effectBlocks[i].state & EFFECT_STATE_PLAYING)
                    {
                        effectBlocks[i].startTime += pauseLength;
                        effectBlocks[i].triggerRepeatAt += pauseLength;
                    }
                }
            }

            break;
        }

#ifdef SERIAL_PRINT
        _debug_printf(
            "Device control: state=%d (1=enable,2=disable,3=stopAll,4=reset,5=pause,6=continue)\n",
            data->state);
#endif
    }

    void FFBReportHandler::set_effect_operation(const EffectOperationReportData* data)
    {
        pidStateDirty = true;
        EffectBlock* effectBlock = get_effect_block(data->effectBlockIndex);
        if (nullptr == effectBlock)
            return;

        switch (data->effectOperation)
        {
            case 1: // start
            {
                effectBlock->effectData.duration = effectBlock->originalDuration;
                effectBlock->remainingLoops = data->loopCount == 0 ? 1 : data->loopCount;

                start_effect(effectBlock);
            }

            break;

            case 2: // start solo
            {
                stop_all_effects();

                effectBlock->effectData.duration = effectBlock->originalDuration;
                effectBlock->remainingLoops = data->loopCount == 0 ? 1 : data->loopCount;

                start_effect(effectBlock);
            }

            break;

            case 3: // stop
            {
                stop_effect(effectBlock);
            }

            break;
        }

#ifdef SERIAL_PRINT
        _debug_printf(
            "Effect operation: idx=%d op=%d loopCount=%d (op: 1=start,2=startSolo,3=stop)\n",
            data->effectBlockIndex, data->effectOperation, data->loopCount);
#endif
    }

    void FFBReportHandler::set_effect_block_free(const BlockFreeReportData* data)
    {
        if (0xFF == data->effectBlockIndex)
            free_all_effects();
        else
            free_effect(data->effectBlockIndex);

        pidStateDirty = true;

#ifdef SERIAL_PRINT
        _debug_printf("Block free: idx=%d (255=all)\n", data->effectBlockIndex);
#endif
    }

    void FFBReportHandler::publish_effect_state(EffectBlock& block, bool playing)
    {
        const uint8_t i = &block - (EffectBlock*)effectBlocks;
        block.actualPlaying = playing;
        pendingEffects[i] = true;
        pendingEffectStates[i] = ((i + 1) << 1) | (playing ? 1 : 0);
        ++effectRevisions[i];
        pidStates.effectBlockIndex = pendingEffectStates[i];
        pidStateDirty = true;
    }

    bool FFBReportHandler::peek_pid_state_report(PIDStateReportData& report,
                                                 uint32_t& revision) const
    {
        for (uint8_t n = 0; n < MAX_EFFECTS; ++n)
        {
            const uint8_t i = (reportCursor + n) % MAX_EFFECTS;
            if (pendingEffects[i])
            {
                report = {pidStates.status, pendingEffectStates[i]};
                revision = effectRevisions[i];
                return true;
            }
        }

        report = *get_pid_state_report_data();
        revision = 0;
        return pidStateDirty;
    }

    void FFBReportHandler::acknowledge_pid_state_report(const PIDStateReportData& report,
                                                        uint32_t revision)
    {
        const uint8_t id = report.effectBlockIndex >> 1;
        if (revision && id && id <= MAX_EFFECTS && effectRevisions[id - 1] == revision)
        {
            pendingEffects[id - 1] = false;
            reportCursor = id % MAX_EFFECTS;
        }

        pidStateDirty = report.status != pidStates.status;

        for (bool pending : pendingEffects)
            pidStateDirty = pidStateDirty || pending;
    }

    bool FFBReportHandler::advance_playback(EffectBlock& b, uint32_t now)
    {
        if ((int32_t)(b.startTime - now) > 0)
            return false;

        const uint32_t elapsed = now - b.startTime;
        const uint16_t duration = b.effectData.duration;
        if (duration != USB_DURATION_INFINITE && elapsed >= duration)
        {
            const uint32_t completed = duration ? elapsed / duration : 0;
            if (duration && (b.remainingLoops == 0xFF || completed < b.remainingLoops))
            {
                if (b.remainingLoops != 0xFF)
                    b.remainingLoops -= completed;
                b.startTime += completed * duration;
                b.sampleValid = false;
            }
            else
            {
                b.triggerRepeatAt = b.startTime + uint32_t(duration) * b.remainingLoops +
                                    b.effectData.triggerRepeatInterval;
                b.remainingLoops = 0;
                if (b.actualPlaying)
                    publish_effect_state(b, false);
                return false;
            }
        }

        if (!b.actualPlaying)
            publish_effect_state(b, true);
        return true;
    }

    bool FFBReportHandler::is_trigger_playing(EffectBlock& b, uint8_t buttons, uint32_t now)
    {
        const uint8_t button = b.effectData.triggerButton;
        if (!button || button > 8)
            return false;

        const bool pressed = (buttons >> (button - 1)) & 1;
        const bool rising = pressed && !b.triggerButtonLatch;
        b.triggerButtonLatch = pressed;
        if (b.triggerRunning)
        {
            if (advance_playback(b, now))
                return true;

            if ((int32_t)(b.startTime - now) > 0)
                return false;

            b.triggerRunning = false;
            b.triggerRepeatPending = b.effectData.triggerRepeatInterval != USB_DURATION_INFINITE;
        }

        const bool repeat =
            pressed && b.triggerRepeatPending && (int32_t)(now - b.triggerRepeatAt) >= 0;
        if (rising || repeat)
        {
            b.startTime = now + b.effectData.startDelay;
            b.remainingLoops = b.loopCount;
            b.triggerRunning = true;
            b.triggerRepeatPending = false;
            b.sampleValid = false;
            return advance_playback(b, now);
        }

        if (!pressed)
            b.triggerRepeatPending = false;
        return false;
    }

    bool FFBReportHandler::is_effect_playing(uint8_t id, uint8_t buttons, uint32_t now)
    {
        EffectBlock* b = get_effect_block(id);
        return b && is_effect_playing(*b, buttons, now);
    }

    bool FFBReportHandler::is_effect_playing(EffectBlock& b, uint8_t buttons, uint32_t now)
    {
        if (!(b.state & EFFECT_STATE_PLAYING))
            return false;

        if (devicePaused)
            return b.actualPlaying;

        if (b.effectData.triggerButton != USB_NO_TRIGGER_BUTTON)
            return is_trigger_playing(b, buttons, now);

        const bool playing = advance_playback(b, now);
        if (!playing && !(int32_t(b.startTime - now) > 0))
            b.state &= ~EFFECT_STATE_PLAYING;
        return playing;
    }
} // namespace SunFFB
