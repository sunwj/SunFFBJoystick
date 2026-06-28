#ifndef _FFB_REPORT_TYPES_H_
#define _FFB_REPORT_TYPES_H_

#include <stdint.h>
#include "constants.h"

namespace SunFFB
{
    // HID input report 1: button state + axis positions (sent at POLLING_RATE)
    struct __attribute__((packed)) JoystickInputReportData
    {
        uint8_t buttons;                                // Bits: 0..7, 8 buttons
        int16_t axis[NUM_AXIS];                         // axis, min -USB_AXIS_MAX_ABSOLUTE, max USB_AXIS_MAX_ABSOLUTE
    };
    
    // HID input report 2: PID device status + currently playing effect
    struct __attribute__((packed)) PIDStateReportData
    {
        uint8_t status;                                 // Bits: 0=Device Paused,1=Actuators Enabled,2=Safety Switch,3=Actuator Override Switch,4=Actuator Power
        uint8_t effectBlockIndex;                       // Bit0=Effect Playing, Bit1..7=EffectId (1..MAX_EFFECTS)
    };
    
    // HID output report 3: effect creation parameters (type, duration, direction, etc.)
    struct __attribute__((packed)) SetEffectReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint8_t effectType;                             // min 1, max NUM_SUPPORTED_EFFECTS
        uint16_t duration;                              // min 0, max 0xFFFE
        uint16_t triggerRepeatInterval;                 // min 0, max 0xFFFE
        uint16_t samplePeriod;                          // min 0, max 0xFFFE
        uint8_t gain;                                   // min 0, max 255
        uint8_t triggerButton;                          // min 1, max N
        uint8_t axisEnable;                             // Bits: X, Y (optional), Z (optional), Direction enable
        uint16_t directions[NUM_AXIS];                  // min 0, max 36000
        uint16_t startDelay;                            // min 0, max 0xFFFE
    };
    
    // HID output report 4: envelope (attack/fade) for an existing effect
    struct __attribute__((packed)) SetEnvelopeReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint16_t attackLevel;                           // min 0, max 10000
        uint16_t fadeLevel;                             // min 0, max 10000
        uint16_t attackTime;                            // min 0, max 0xFFFE
        uint16_t fadeTime;                              // min 0, max 0xFFFE
    };
    
    // HID output report 5: condition block (spring/damper/inertia/friction params per axis)
    struct __attribute__((packed)) SetConditionReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint8_t parameterBlockOffset;                   // Bits: 0, min 0, max 1
        int16_t cpOffset;                               // min -USB_AXIS_MAX_ABSOLUTE, max USB_AXIS_MAX_ABSOLUTE
        int16_t positiveCoefficient;                    // min -10000, max 10000
        int16_t negativeCoefficient;                    // min -10000, max 10000
        uint16_t positiveSaturation;                    // min 0, max 10000
        uint16_t negativeSaturation;                    // min 0, max 10000
        uint16_t deadBand;                              // min 0, max USB_AXIS_MAX_ABSOLUTE
    };
    
    // HID output report 6: periodic effect waveform (magnitude, offset, phase, period)
    struct __attribute__((packed)) SetPeriodicReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint16_t magnitude;                             // min 0, max 10000
        int16_t offset;                                 // min -10000, max 10000
        uint16_t phase;                                 // min 0, max 35999
        uint16_t period;                                // min 0, max 0xFFFE
    };
    
    // HID output report 7: constant force magnitude for an effect
    struct __attribute__((packed)) SetConstantForceReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        int16_t magnitude;                              // min -10000, max 10000
    };
    
    // HID output report 8: ramp start/end for an effect
    struct __attribute__((packed)) SetRampForceReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        int16_t rampStart;                              // min -10000, max 10000
        int16_t rampEnd;                                // min -10000, max 10000
    };
    
    // HID output report 11: start/stop/loop control for an effect
    struct __attribute__((packed)) EffectOperationReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint8_t effectOperation;                        // 1 = start, 2 = start solo, 3 = stop
        uint8_t loopCount;                              // min = 0, max = 255
    };
    
    // HID output report 12: free a single effect block or all blocks (0xFF)
    struct __attribute__((packed)) BlockFreeReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
    };
    
    // HID output report 13: device-level control (enable, disable, reset, pause, continue)
    struct __attribute__((packed)) DeviceControlReportData
    {
        uint8_t state;                                  // 1 = enable, 2 = disable, 3 = stop all effects, 4 = reset, 5 = pause, 6 = continue
    };
    
    // HID output report 14: master device gain
    struct __attribute__((packed)) DeviceGainReportData
    {
        uint8_t gain;                                   // min 0, max 255
    };
    
    // HID feature report 16: request a new effect block allocation
    struct __attribute__((packed)) CreateNewEffectReportData
    {
        uint8_t effectType;                             // min = 1, max = NUM_SUPPORTED_EFFECTS
    };
    
    // HID feature report 17: result of effect creation (index + status + pool remaining)
    struct __attribute__((packed)) BlockLoadReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint8_t blockLoadStatus;                        // 1 = success, 2 = full, 3 = error
        uint16_t ramPoolAvailable;                      // min = 0, max = 65535
    };
    
    // HID feature report 18: device pool capacity and management info
    struct __attribute__((packed)) PoolReportData
    {
        uint16_t ramPoolSize;                           // min = 0, max = 65535
        uint8_t maxSimultaneousEffects;                 // min = 0, max = 255
        uint8_t managedPool;                            // Bits0: device managed, Bits1: shared
    };
    
    // Overlay: one TypeSpecificParameterBlock stores effect data for a single axis
    // (periodic/constant/ramp/condition) or the envelope. An EffectBlock holds
    // NUM_AXIS such slots.
    union TypeSpecificParameterBlock
    {
        SetEnvelopeReportData envelopeData;
        SetConditionReportData conditionData;
        SetPeriodicReportData periodicData;
        SetConstantForceReportData constantData;
        SetRampForceReportData rampData;
    };
    
    // Runtime state for one allocated effect. Stored in FFBReportHandler::effectBlocks[].
    struct EffectBlock
    {
        volatile uint8_t state;                         // EFFECT_STATE_FREE / ALLOCATED / PLAYING bits
        uint32_t startTime;                             // timestamp (millis) when effect started
        float directionUnitVector[NUM_AXIS];            // pre-computed unit vector from host direction
        bool envelopParameter;                          // true if envelope has been configured for this effect
        bool triggerButtonLatch;                        // edge-detect state for trigger-button start
        uint8_t conditionBlockFlags = 0x00;             // bitmask: which typeSpecificData slots hold condition data
        uint16_t originalDuration = 0;                  // duration before loop-count multiplication
    
        SetEffectReportData effectData;                                 // common effect parameters
        TypeSpecificParameterBlock typeSpecificData[NUM_AXIS];          // per-axis type-specific data + envelope
    };
    
    // Derived motion metrics for one frame update (not packed — internal use only)
    struct __attribute__((packed)) Metrics
    {
        float position[NUM_AXIS];                       // low-pass filtered position
        float speed[NUM_AXIS];                          // low-pass filtered speed (from position derivative)
        float acceleration[NUM_AXIS];                   // low-pass filtered acceleration (from speed derivative)

        int32_t positionDeadBand[NUM_AXIS] = {0};       // deadband threshold for position
        int32_t speedDeadBand[NUM_AXIS] = {0};          // deadband threshold for speed
        int32_t accelerationDeadBand[NUM_AXIS] = {0};   // deadband threshold for acceleration

        float maxPosition[NUM_AXIS] = {(float)USB_AXIS_MAX_ABSOLUTE};     // normalization ceiling for position
        float maxSpeed[NUM_AXIS] = {(float)USB_AXIS_MAX_ABSOLUTE};        // normalization ceiling for speed
        float maxAcceleration[NUM_AXIS] = {(float)USB_AXIS_MAX_ABSOLUTE}; // normalization ceiling for accel
    };
} // namespace SunFFB

#endif