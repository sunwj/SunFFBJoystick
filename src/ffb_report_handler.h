#ifndef _FFB_REPORT_HANDLER_H_
#define _FFB_REPORT_HANDLER_H_

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include "ffb_report_descriptor.h"
#include "ffb_report_types.h"

namespace SunFFB
{
    // Manages effect block lifecycle, PID state, and HID report data.
    // Shared between USB callback (core 0) and force_calculation_task (core 1);
    // guarded by semaphoreFFBReportHandler.
    class FFBReportHandler
    {
        public:
        // Device power state machine — gates force calculation in FFBForceCalculator.
        enum DeviceState : uint8_t {
            DEVICE_STATE_INIT = 0,      // after reset, before first enable
            DEVICE_STATE_ACTIVE = 1,    // actuators enabled, normal operation
            DEVICE_STATE_PAUSED = 2,    // paused by host, forces held at zero
            DEVICE_STATE_DISABLED = 3,  // actuators disabled by host
        };

        FFBReportHandler();
        ~FFBReportHandler() {free_all_effects();};
        void init();                            // Reset state + free all effects.

        // PID report accessors (read by send_report_task / hid_get_report_callback).
        const PIDStateReportData* get_pid_state_report_data() const {return (const PIDStateReportData*)&pidStates;};
        const BlockLoadReportData* get_block_load_report_data() const {return (const BlockLoadReportData*)&blockLoadData;};
        const PoolReportData* get_pool_report_data();

        // Effect lifecycle: allocate, query free slot, free, read all blocks.
        void create_new_effect(const CreateNewEffectReportData* data);
        uint8_t get_next_free_effect_block_index();
        void free_effect(uint8_t idx);
        EffectBlock* get_all_effect_blocks() const {return (EffectBlock*)effectBlocks;};

        // HID output report handlers (called from hid_set_report_callback).
        void set_effect(const SetEffectReportData* data);          // Report 3
        void set_envelope(const SetEnvelopeReportData* data);      // Report 4
        void set_condition(const SetConditionReportData* data);    // Report 5
        void set_periodic(const SetPeriodicReportData* data);      // Report 6
        void set_constant_force(const SetConstantForceReportData* data);  // Report 7
        void set_ramp_force(const SetRampForceReportData* data);   // Report 8
        void set_effect_operation(const EffectOperationReportData* data);  // Report 11
        void set_effect_block_free(const BlockFreeReportData* data);      // Report 12

        // Device-level control handlers (called from hid_set_report_callback).
        void set_device_gain(const DeviceGainReportData* data);          // Report 14
        void set_device_control(const DeviceControlReportData* data);    // Report 13

        // Play-state check (1-based effectBlockIndex). May auto-stop expired effects.
        bool is_effect_playing(uint8_t effectBlockIndex, uint8_t triggerButtonState, uint32_t currentTime);

        volatile bool devicePaused;                     // true when host has paused the device
        volatile DeviceState deviceState = DEVICE_STATE_INIT;    // current power state
        volatile uint8_t deviceGain = USB_MAX_DEVICE_GAIN;       // master gain (scales all forces)
        volatile bool pidStateDirty = false;            // set by state changes, cleared by send_report_task

        private:
        EffectBlock* get_effect_block(uint8_t idx) const;       // 1-based → 0-based lookup
        void free_all_effects();                                // free all blocks, reset pool
        void start_effect(volatile EffectBlock* effectBlock);   // set PLAYING + init startTime
        void stop_effect(volatile EffectBlock* effectBlock);    // clear PLAYING + update pidStates
        void stop_all_effects();                                // stop all + clear pidStates.effectBlockIndex
        bool is_trigger_playing(volatile EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime);
        void update_pid_effect_index();                         // scan for first playing, set pidStates

        uint8_t nextEffectIdx = 0;                              // round-robin allocator cursor
        volatile uint32_t pauseTime;                            // timestamp when paused (for resume adjustment)
        volatile EffectBlock effectBlocks[MAX_EFFECTS];         // effect block pool
        volatile PIDStateReportData pidStates = {0x1E, 0};      // PID state (status + playing effect index)
        volatile BlockLoadReportData blockLoadData;             // last creation result
        volatile PoolReportData poolData;                       // cached pool capacity info
    };
} // namespace SunFFB

#endif