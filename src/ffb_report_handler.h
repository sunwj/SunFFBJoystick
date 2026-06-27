#ifndef _FFB_REPORT_HANDLER_H_
#define _FFB_REPORT_HANDLER_H_

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "ffb_report_descriptor.h"
#include "ffb_report_types.h"

namespace SunFFB
{
    class FFBReportHandler
    {
        public:
        enum DeviceState : uint8_t {
            DEVICE_STATE_INIT = 0,
            DEVICE_STATE_ACTIVE = 1,
            DEVICE_STATE_PAUSED = 2,
            DEVICE_STATE_DISABLED = 3,
        };

        FFBReportHandler();
        ~FFBReportHandler() {free_all_effects();};
        void init();
        const PIDStateReportData* get_pid_state_report_data() const {return (const PIDStateReportData*)&pidStates;};
        const BlockLoadReportData* get_block_load_report_data() const {return (const BlockLoadReportData*)&blockLoadData;};
        const PoolReportData* get_pool_report_data();

        void create_new_effect(const CreateNewEffectReportData* data);
        uint8_t get_next_free_effect_block_index();
        void free_effect(uint8_t idx);
        const EffectBlock* get_all_effect_blocks() const {return (const EffectBlock*)effectBlocks;};

        void set_effect(const SetEffectReportData* data);
        void set_envelope(const SetEnvelopeReportData* data);
        void set_condition(const SetConditionReportData* data);
        void set_periodic(const SetPeriodicReportData* data);
        void set_constant_force(const SetConstantForceReportData* data);
        void set_ramp_force(const SetRampForceReportData* data);
        void set_effect_operation(const EffectOperationReportData* data);
        void set_effect_block_free(const BlockFreeReportData* data);

        void set_device_gain(const DeviceGainReportData* data);
        void set_device_control(const DeviceControlReportData* data);

        volatile bool devicePaused;
        volatile DeviceState deviceState = DEVICE_STATE_INIT;
        volatile uint8_t deviceGain = USB_MAX_DEVICE_GAIN;

        private:
        EffectBlock* get_effect_block(uint8_t idx);
        void free_all_effects();
        void start_effect(volatile EffectBlock* effectBlock);
        void stop_effect(volatile EffectBlock* effectBlock) {effectBlock->state &= ~EFFECT_STATE_PLAYING;};
        void stop_all_effects();

        uint8_t nextEffectIdx = 0;
        volatile uint32_t pauseTime;
        volatile EffectBlock effectBlocks[MAX_EFFECTS];
        volatile PIDStateReportData pidStates = {0x1E, 0};
        volatile BlockLoadReportData blockLoadData;
        volatile PoolReportData poolData;
    };
} // namespace SunFFB

#endif