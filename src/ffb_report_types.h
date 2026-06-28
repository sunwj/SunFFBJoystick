#ifndef _FFB_REPORT_TYPES_H_
#define _FFB_REPORT_TYPES_H_

#include <stdint.h>
#include "constants.h"

namespace SunFFB
{
    struct __attribute__((packed)) JoystickInputReportData
    {
        uint8_t buttons;                                // Bits: 0..7, 8 buttons
        int16_t axis[NUM_AXIS];                         // axis, min -USB_AXIS_MAX_ABSOLUTE, max USB_AXIS_MAX_ABSOLUTE
    };
    
    struct __attribute__((packed)) PIDStateReportData
    {
        uint8_t status;                                 // Bits: 0=Device Paused,1=Actuators Enabled,2=Safety Switch,3=Actuator Override Switch,4=Actuator Power
        uint8_t effectBlockIndex;                       // Bit0=Effect Playing, Bit1..7=EffectId (1..MAX_EFFECTS)
    };
    
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
    
    struct __attribute__((packed)) SetEnvelopeReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint16_t attackLevel;                           // min 0, max 10000
        uint16_t fadeLevel;                             // min 0, max 10000
        uint16_t attackTime;                            // min 0, max 0xFFFE
        uint16_t fadeTime;                              // min 0, max 0xFFFE
    };
    
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
    
    struct __attribute__((packed)) SetPeriodicReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint16_t magnitude;                             // min 0, max 10000
        int16_t offset;                                 // min -10000, max 10000
        uint16_t phase;                                 // min 0, max 35999
        uint16_t period;                                // min 0, max 0xFFFE
    };
    
    struct __attribute__((packed)) SetConstantForceReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        int16_t magnitude;                              // min -10000, max 10000
    };
    
    struct __attribute__((packed)) SetRampForceReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        int16_t rampStart;                              // min -10000, max 10000
        int16_t rampEnd;                                // min -10000, max 10000
    };
    
    struct __attribute__((packed)) EffectOperationReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint8_t effectOperation;                        // 1 = start, 2 = start solo, 3 = stop
        uint8_t loopCount;                              // min = 0, max = 255
    };
    
    struct __attribute__((packed)) BlockFreeReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
    };
    
    struct __attribute__((packed)) DeviceControlReportData
    {
        uint8_t state;                                  // 1 = enable, 2 = disable, 3 = stop all effects, 4 = reset, 5 = pause, 6 = continue
    };
    
    struct __attribute__((packed)) DeviceGainReportData
    {
        uint8_t gain;                                   // min 0, max 255
    };
    
    struct __attribute__((packed)) CreateNewEffectReportData
    {
        uint8_t effectType;                             // min = 1, max = NUM_SUPPORTED_EFFECTS
    };
    
    struct __attribute__((packed)) BlockLoadReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max MAX_EFFECTS
        uint8_t blockLoadStatus;                        // 1 = success, 2 = full, 3 = error
        uint16_t ramPoolAvailable;                      // min = 0, max = 65535
    };
    
    struct __attribute__((packed)) PoolReportData
    {
        uint16_t ramPoolSize;                           // min = 0, max = 65535
        uint8_t maxSimultaneousEffects;                 // min = 0, max = 255
        uint8_t managedPool;                            // Bits0: device managed, Bits1: shared
    };
    
    union TypeSpecificParameterBlock
    {
        SetEnvelopeReportData envelopeData;
        SetConditionReportData conditionData;
        SetPeriodicReportData periodicData;
        SetConstantForceReportData constantData;
        SetRampForceReportData rampData;
    };
    
    struct EffectBlock
    {
        volatile uint8_t state;
        uint32_t startTime;
        float directionUnitVector[NUM_AXIS];
        bool envelopParameter;
        bool triggerButtonLatch;
        uint8_t conditionBlockFlags = 0x00;
        uint16_t originalDuration = 0;
    
        SetEffectReportData effectData;
        TypeSpecificParameterBlock typeSpecificData[NUM_AXIS];
    };
    
    struct __attribute__((packed)) Metrics
    {
        float position[NUM_AXIS];
        float speed[NUM_AXIS];
        float acceleration[NUM_AXIS];

        int32_t positionDeadBand[NUM_AXIS] = {0};
        int32_t speedDeadBand[NUM_AXIS] = {0};
        int32_t accelerationDeadBand[NUM_AXIS] = {0};

        float maxPosition[NUM_AXIS] = {(float)USB_AXIS_MAX_ABSOLUTE};
        float maxSpeed[NUM_AXIS] = {(float)USB_AXIS_MAX_ABSOLUTE};
        float maxAcceleration[NUM_AXIS] = {(float)USB_AXIS_MAX_ABSOLUTE};
    };
} // namespace SunFFB

#endif