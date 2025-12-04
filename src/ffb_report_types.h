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
        uint8_t effectBlockIndex;                       // Bit0=Effect Playing, Bit1..7=EffectId (1..40)
    };
    
    struct __attribute__((packed)) SetEffectReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max 40
        uint8_t effectType;                             // min 1, max NUM_SUPPORTED_EFFECTS
        uint16_t duration;                              // min 0, max 0xFFFF
        uint16_t triggerRepeatInterval;                 // min 0, max 0xFFFF
        uint16_t samplePeriod;                          // min 0, max 0xFFFF
        uint8_t gain;                                   // min 0, max 255
        uint8_t triggerButton;                          // min 1, max 8
        uint8_t axisEnable;                             // Bits: 0 = X, 1 = Y, 2 = Direction enable
        uint16_t directionX;                            // min 0, max 62831 = 2PI * 10000 
        uint16_t directionY;                            // min 0, max 62831 = 2PI * 10000
        uint16_t startDelay;                            // min 0, max 0xFFFF
    };
    
    struct __attribute__((packed)) SetEnvelopeReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max 40
        uint16_t attackLevel;                           // min 0, max 10000
        uint16_t fadeLevel;                             // min 0, max 10000
        uint16_t attackTime;                            // min 0, max 0xFFFF
        uint16_t fadeTime;                              // min 0, max 0xFFFF
    };
    
    struct __attribute__((packed)) SetConditionReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max 40
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
        uint8_t effectBlockIndex;                       // min 1, max 40
        uint16_t magnitude;                             // min 0, max 10000
        int16_t offset;                                 // min -10000, max 10000
        uint16_t phase;                                 // min 0, max 62831
        uint16_t period;                                // min 0, max 0xFFFF
    };
    
    struct __attribute__((packed)) SetConstantForceReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max 40
        int16_t magnitude;                              // min -10000, max 10000
    };
    
    struct __attribute__((packed)) SetRampForceReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max 40
        int16_t rampStart;                              // min -10000, max 10000
        int16_t rampEnd;                                // min -10000, max 10000
    };
    
    struct __attribute__((packed)) EffectOperationReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max 40
        uint8_t effectOperation;                        // 1 = start, 2 = start solo, 3 = stop
        uint8_t loopCount;                              // min = 0, max = 255
    };
    
    struct __attribute__((packed)) BlockFreeReportData
    {
        uint8_t effectBlockIndex;                       // min 1, max 40
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
        uint8_t effectBlockIndex;                       // min 1, max 40
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
        uint8_t conditionBlockCount = 0;
    
        SetEffectReportData effectData;
        TypeSpecificParameterBlock typeSpecificData[NUM_AXIS];
    };
    
    struct __attribute__((packed)) Metrics
    {
        int32_t postion[NUM_AXIS];
        int32_t speed[NUM_AXIS];
        int32_t acceleration[NUM_AXIS];

        int32_t positionDeadBand[NUM_AXIS] = {0};
        int32_t speedDeadBand[NUM_AXIS] = {0};
        int32_t accelerationDeadBand[NUM_AXIS] = {0};

        int32_t maxPosition[NUM_AXIS] = {USB_AXIS_MAX_ABSOLUTE};
        int32_t maxSpeed[NUM_AXIS] = {USB_AXIS_MAX_ABSOLUTE};
        int32_t maxAcceleration[NUM_AXIS] = {USB_AXIS_MAX_ABSOLUTE};
    };
} // namespace SunFFB

#endif