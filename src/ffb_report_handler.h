/**
 * Device state machine and bounded effect pool; host effect identifiers are one-based.
 * Allocation, configuration, playback and release are distinct stages; allocation does not start force.
 * Callers synchronize cross-task access. Returned report and effect pointers remain handler-owned.
 */

#ifndef _FFB_REPORT_HANDLER_H_
#define _FFB_REPORT_HANDLER_H_

#include <stdint.h>
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
        enum DeviceState : uint8_t
        {
            DEVICE_STATE_INIT = 0,     // after reset, before first enable
            DEVICE_STATE_ACTIVE = 1,   // actuators enabled, normal operation
            DEVICE_STATE_PAUSED = 2,   // paused by host, forces held at zero
            DEVICE_STATE_DISABLED = 3, // actuators disabled by host
        };

        FFBReportHandler();
        ~FFBReportHandler()
        {
            free_all_effects();
        };

        void init(); // Reset state + free all effects.

        // PID report accessors (read by send_report_task / hid_get_report_callback).
        const PIDStateReportData* get_pid_state_report_data() const
        {
            return (const PIDStateReportData*)&pidStates;
        };

        bool peek_pid_state_report(PIDStateReportData& report, uint32_t& revision) const;
        // Acknowledge only after USB accepts the peeked report; matching revision preserves newer state.
        void acknowledge_pid_state_report(const PIDStateReportData& report, uint32_t revision);
        const BlockLoadReportData* get_block_load_report_data() const
        {
            return (const BlockLoadReportData*)&blockLoadData;
        };

        const PoolReportData* get_pool_report_data();

        // Effect lifecycle: allocate, query free slot, free, read all blocks.
        void create_new_effect(const CreateNewEffectReportData* data);
        uint8_t get_next_free_effect_block_index();
        void free_effect(uint8_t idx);
        EffectBlock* get_all_effect_blocks() const
        {
            return (EffectBlock*)effectBlocks;
        };

        // HID output handlers dispatched by hid_command_task after copying and validating USB bytes.
        // None of these methods supplies its own mutex; direct callers must preserve the lock contract.
        void set_effect(const SetEffectReportData* data);                 // Report 3
        void set_envelope(const SetEnvelopeReportData* data);             // Report 4
        void set_condition(const SetConditionReportData* data);           // Report 5
        void set_periodic(const SetPeriodicReportData* data);             // Report 6
        void set_constant_force(const SetConstantForceReportData* data);  // Report 7
        void set_ramp_force(const SetRampForceReportData* data);          // Report 8
        void set_effect_operation(const EffectOperationReportData* data); // Report 11
        void set_effect_block_free(const BlockFreeReportData* data);      // Report 12

        // Device-level control handlers (called from hid_set_report_callback).
        void set_device_gain(const DeviceGainReportData* data);       // Report 14
        void set_device_control(const DeviceControlReportData* data); // Report 13

        // Play-state check (one-based effect ID). Advances loops/triggers and publishes actual transitions.
        // DEVICE_STATE_DISABLED can still advance timelines; the calculator gates force separately.
        bool is_effect_playing(uint8_t effectBlockIndex, uint8_t triggerButtonState,
                               uint32_t currentTime);
        bool is_effect_playing(EffectBlock& effectBlock, uint8_t triggerButtonState,
                               uint32_t currentTime);

        volatile bool devicePaused; // true when host has paused the device
        volatile DeviceState deviceState = DEVICE_STATE_INIT; // current power state
        volatile uint8_t deviceGain = USB_MAX_DEVICE_GAIN;    // master gain (scales all forces)

        volatile bool pidStateDirty =
            false; // per-effect pending reports; acknowledged after USB acceptance

        private:
        EffectBlock* get_effect_block(uint8_t idx) const; // 1-based → 0-based lookup
        void free_all_effects();                          // free all blocks, reset pool
        void start_effect(EffectBlock* effectBlock);      // set PLAYING + init startTime
        void stop_effect(EffectBlock* effectBlock);       // clear PLAYING + update pidStates
        void stop_all_effects(); // stop all + clear pidStates.effectBlockIndex

        bool is_trigger_playing(EffectBlock& effectBlock, uint8_t triggerButtonState,
                                uint32_t currentTime);
        void publish_effect_state(EffectBlock& block, bool playing);
        bool advance_playback(EffectBlock& block,
                              uint32_t now); // advance duration/loops, publish actual transitions

        // Bounded storage keeps the newest state for every effect, even during USB backpressure.
        bool pendingEffects[MAX_EFFECTS] = {};
        uint8_t pendingEffectStates[MAX_EFFECTS] = {};
        uint32_t effectRevisions[MAX_EFFECTS] = {};
        uint8_t reportCursor = 0;

        uint8_t nextEffectIdx = 0; // round-robin allocator cursor

        bool actuatorsEnabled = false;
        bool actuatorsInitialized = false;

        void update_device_state();

        uint32_t pauseTime; // timestamp when paused (for resume adjustment)

        volatile EffectBlock effectBlocks[MAX_EFFECTS] = {}; // effect block pool
        volatile PIDStateReportData pidStates = {0x1C,
                                                 0}; // PID state (status + playing effect index)
        BlockLoadReportData blockLoadData;           // last creation result
        PoolReportData poolData;                     // cached pool capacity info
    };
} // namespace SunFFB

#endif
