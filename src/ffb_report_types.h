#ifndef _FFB_REPORT_TYPES_H_
#define _FFB_REPORT_TYPES_H_

#include <Arduino.h>


#define NUM_AXIS 2
#define X_AXIS_ENABLE 0x01
#define Y_AXIS_ENABLE 0x02
#define DIRECTION_ENABLE 0x04
#define TYPE_SPECIFIC_BLOCK_OFFSET_1 0
#define TYPE_SPECIFIC_BLOCK_OFFSET_2 1

#define USB_DURATION_INFINITE 0xFFFF
#define USB_MAX_EFFECT_GAIN 255
#define USB_MAX_DEVICE_GAIN 255
#define USB_NO_TRIGGER_BUTTON 0xFF
#define USB_MAX_MAGNITUDE 10000
#define USB_NORMALIZATION_MAGNITUDE 0.0001f
#define USB_AXIS_MAX_ABSOLUTE 32767
#define USB_MAX_PHASE 62830
#define USB_NORMALIZATION_RAD 0.0001f

#define EFFECT_STATE_FREE 0x00
#define EFFECT_STATE_ALLOCATED 0x01
#define EFFECT_STATE_PLAYING 0x02

#define DEVICE_STATE_DEFAULT_SPRING 0x01

// order must be kept the same with the set effect report definition
#define ET_CONSTANT 0x01
#define ET_RAMP 0x02
#define ET_SQUARE 0x03
#define ET_SINE 0x04
#define ET_TRIANGLE 0x05
#define ET_SAWTOOTH_UP 0x06
#define ET_SAWTOOTH_DOWN 0x07
#define ET_SPRING 0x08
#define ET_DAMPER 0x09
#define ET_INERTIA 0x0A
#define ET_FRICTION 0x0B

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