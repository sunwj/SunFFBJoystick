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
        FFBReportHandler();
        ~FFBReportHandler() {free_all_effects();};
        void init();
        const PIDStateReportData* get_pid_state_report_data() const {return (const PIDStateReportData*)&pidStates;};
        const BlockLoadReportData* get_block_load_report_data() const {return (const BlockLoadReportData*)&blockLoadData;};
        PoolReportData* get_pool_report_data();

        void create_new_effect();
        uint8_t get_next_free_effect_block_index();
        void free_effect(uint8_t idx);
        const EffectBlock* get_all_effect_blocks() const {return (const EffectBlock*)effectBlocks;};

        void set_effect(SetEffectReportData* data);
        void set_envelop(SetEnvelopeReportData* data);
        void set_condition(SetConditionReportData* data);
        void set_periodic(SetPeriodicReportData* data);
        void set_constant_force(SetConstantForceReportData* data);
        void set_ramp_force(SetRampForceReportData* data);
        void set_effect_operation(EffectOperationReportData* data);
        void set_effect_block_free(BlockFreeReportData* data);

        void set_device_gain(DeviceGainReportData* data);
        void set_device_control(DeviceControlReportData* data);

        volatile bool devicePaused;
        volatile uint8_t deviceState;
        volatile uint8_t deviceGain = USB_MAX_DEVICE_GAIN;

        private:
        EffectBlock* get_effect_block(uint8_t idx);
        void free_all_effects();
        void start_effect(volatile EffectBlock* effectBlock);
        void stop_effect(volatile EffectBlock* effectBlock) {effectBlock->state &= ~EFFECT_STATE_PLAYING;};
        void stop_all_effects();

        uint32_t pauseTime;
        uint8_t nextEffectIdx = 0;

        volatile EffectBlock effectBlocks[MAX_EFFECTS];
        volatile PIDStateReportData pidStates = {0x1E, 0};
        volatile BlockLoadReportData blockLoadData;
        volatile PoolReportData poolData;
    };
} // namespace SunFFB

#endif