#ifndef _FFB_DESCRIPTOR_H_
#define _FFB_DESCRIPTOR_H_

#include <Adafruit_TinyUSB.h>
#include "hid_pid.h"
#include "constants.h"


#if (NUM_AXIS == 1)
#define ENABLED_AXIS HID_USAGE(HID_USAGE_DESKTOP_X)
#elif (NUM_AXIS == 2)
#define ENABLED_AXIS HID_USAGE(HID_USAGE_DESKTOP_X), \
                     HID_USAGE(HID_USAGE_DESKTOP_Y)
#elif (NUM_AXIS == 3)
#define ENABLED_AXIS HID_USAGE(HID_USAGE_DESKTOP_X), \
                     HID_USAGE(HID_USAGE_DESKTOP_Y), \
                     HID_USAGE(HID_USAGE_DESKTOP_Z)
#endif

#if (NUM_AXIS == 1)
#define DIRECTIONS HID_USAGE(1)
#elif (NUM_AXIS == 2)
#define DIRECTIONS HID_USAGE(1), \
                   HID_USAGE(2)
#elif (NUM_AXIS == 3)
#define DIRECTIONS HID_USAGE(1), \
                   HID_USAGE(2), \
                   HID_USAGE(3)
#endif

/////////////////////////////////////////////////////////////////////
// Input Report: Joystick postition, buttons, etc.
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_INPUT_JOYSTICK \
    HID_REPORT_ID(REPORT_ID_JOYSTICK) \
    HID_COLLECTION(HID_COLLECTION_PHYSICAL), \
        HID_USAGE_PAGE(HID_USAGE_PAGE_BUTTON), \
        HID_USAGE_MIN(1), \
        HID_USAGE_MAX(8), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX(1), \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(8), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),\
        HID_COLLECTION(HID_COLLECTION_PHYSICAL),\
            ENABLED_AXIS, \
            HID_LOGICAL_MIN_N(-USB_AXIS_MAX_ABSOLUTE, 2),\
            HID_LOGICAL_MAX_N(USB_AXIS_MAX_ABSOLUTE, 2),\
            HID_REPORT_SIZE(16),\
            HID_REPORT_COUNT(NUM_AXIS),\
            HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE),\
        HID_COLLECTION_END,\
    HID_COLLECTION_END
        
#define FFB_REPORT_DESC_INPUT_PID_STATE \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID) , \
    HID_USAGE(HID_USAGE_PID_STATE_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_PID_STATE) \
        HID_USAGE(HID_USAGE_PID_DEVICE_PAUSED), \
        HID_USAGE(HID_USAGE_PID_ACTUATORS_ENABLED), \
        HID_USAGE(HID_USAGE_PID_SAFETY_SWITCH), \
        HID_USAGE(HID_USAGE_PID_ACTUATOR_OVERRIDE_SWITCH), \
        HID_USAGE(HID_USAGE_PID_ACTUATOR_POWER), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX(1), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX(1), \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(5), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        HID_REPORT_COUNT(3), \
        HID_INPUT(HID_DATA | HID_CONSTANT | HID_VARIABLE), \
        \
        HID_USAGE(HID_USAGE_PID_EFFECT_PLAYING), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX(1), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX(1), \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(1), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(7), \
        HID_REPORT_COUNT(1), \
        HID_INPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
    HID_COLLECTION_END



/////////////////////////////////////////////////////////////////////
// Output Report: Set Effect - define params for an effect
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_SET_EFFECT \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_SET_EFFECT_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_SET_EFFECT_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Effect Type */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_TYPE), \
        HID_COLLECTION(HID_COLLECTION_LOGICAL), \
            HID_USAGE(HID_USAGE_PID_ET_CONSTANT_FORCE), \
            HID_USAGE(HID_USAGE_PID_ET_RAMP), \
            HID_USAGE(HID_USAGE_PID_ET_SQUARE), \
            HID_USAGE(HID_USAGE_PID_ET_SINE), \
            HID_USAGE(HID_USAGE_PID_ET_TRIANGLE), \
            HID_USAGE(HID_USAGE_PID_ET_SAWTOOTH_UP), \
            HID_USAGE(HID_USAGE_PID_ET_SAWTOOTH_DOWN), \
            HID_USAGE(HID_USAGE_PID_ET_SPRING), \
            HID_USAGE(HID_USAGE_PID_ET_DAMPER), \
            HID_USAGE(HID_USAGE_PID_ET_INERTIA), \
            HID_USAGE(HID_USAGE_PID_ET_FRICTION), \
            /* HID_USAGE(HID_USAGE_PID_ET_CUSTOM_FORCE), */ \
            HID_LOGICAL_MIN(1), \
            HID_LOGICAL_MAX(NUM_SUPPORTED_EFFECTS), \
            HID_PHYSICAL_MIN(1), \
            HID_PHYSICAL_MAX(NUM_SUPPORTED_EFFECTS), \
            HID_REPORT_SIZE(8), \
            HID_REPORT_COUNT(1), \
            HID_OUTPUT(HID_DATA | HID_ARRAY | HID_ABSOLUTE), \
        HID_COLLECTION_END, \
        \
        /* Duration, Trigger Repeat, Sample Period */ \
        HID_USAGE(HID_USAGE_PID_DURATION), \
        HID_USAGE(HID_USAGE_PID_TRIGGER_REPEAT_INTERVAL), \
        HID_USAGE(HID_USAGE_PID_SAMPLE_PERIOD), \
        HID_UNIT_N(HID_UNIT_SECONDS, 2), \
        HID_UNIT_EXPONENT(0xFD), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(0xFFFE, 3), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(0xFFFE, 3), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(3), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Gain */ \
        HID_USAGE(HID_USAGE_PID_GAIN), \
        HID_UNIT_EXPONENT(0), \
        HID_UNIT(0), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(255, 2), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Trigger Button */ \
        HID_USAGE(HID_USAGE_PID_TRIGGER_BUTTON), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(8), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(8), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Axes Enable */ \
        HID_USAGE(HID_USAGE_PID_AXES_ENABLE), \
        HID_COLLECTION(HID_COLLECTION_LOGICAL), \
            HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP), \
            ENABLED_AXIS, \
            HID_LOGICAL_MIN(0), \
            HID_LOGICAL_MAX(1), \
            HID_REPORT_SIZE(1), \
            HID_REPORT_COUNT(NUM_AXIS), \
            HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        HID_COLLECTION_END, \
        \
        /* Direction Enable */ \
        HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
        HID_USAGE(HID_USAGE_PID_DIRECTION_ENABLE), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        HID_REPORT_COUNT(8 - NUM_AXIS - 1), \
        HID_OUTPUT(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Direction */ \
        HID_USAGE(HID_USAGE_PID_DIRECTION), \
        HID_COLLECTION(HID_COLLECTION_LOGICAL), \
            /* NOTE: The HID spec specifies these should be Joystick usages, but Windows expects Ordinal.*/ \
            /* TODO: check Linux and try to reconcile */ \
            HID_USAGE_PAGE(HID_USAGE_PAGE_ORDINAL), \
            DIRECTIONS, \
            HID_UNIT(20), \
            HID_UNIT_EXPONENT(0xFE), \
            HID_LOGICAL_MIN(0), \
            HID_LOGICAL_MAX_N(36000, 3), \
            HID_PHYSICAL_MIN(0), \
            HID_PHYSICAL_MAX_N(36000, 3), \
            HID_UNIT(0), \
            HID_REPORT_SIZE(16), \
            HID_REPORT_COUNT(NUM_AXIS), \
            HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
            HID_UNIT_EXPONENT(0), \
            HID_UNIT(0), \
        HID_COLLECTION_END, \
        \
        /* Start Delay */ \
        HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
        HID_USAGE(HID_USAGE_PID_START_DELAY), \
        HID_UNIT_N(HID_UNIT_SECONDS, 2), \
        HID_UNIT_EXPONENT(0xFD), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(0xFFFE, 3), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(0xFFFE, 3), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        HID_UNIT(0), \
        HID_UNIT_EXPONENT(0), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Set Envelope - define envelope for an effect
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_SET_ENVELOPE \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_SET_ENVELOPE_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_SET_ENVELOPE_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Attack Level, Fade Level */ \
        HID_USAGE(HID_USAGE_PID_ATTACK_LEVEL), \
        HID_USAGE(HID_USAGE_PID_FADE_LEVEL), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(10000, 2), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(2), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Attack Time, Fade Time */ \
        HID_USAGE(HID_USAGE_PID_ATTACK_TIME), \
        HID_USAGE(HID_USAGE_PID_FADE_TIME), \
        HID_UNIT_N(HID_UNIT_SECONDS, 2), \
        HID_UNIT_EXPONENT(0xFD), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(0xFFFE, 3), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(0xFFFE, 3), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(2), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        HID_PHYSICAL_MAX(0), \
        HID_UNIT(0), \
        HID_UNIT_EXPONENT(0), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Set Condition - params for spring/damper/inertia
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_SET_CONDITION \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_SET_CONDITION_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_SET_CONDITION_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Parameter Block Offset */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_OFFSET), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX(1), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX(1), \
        HID_REPORT_SIZE(8), /* 4 in SW FF2, since Specific Block Offset is also given afterward */ \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Center-Point Offset */ \
        HID_USAGE(HID_USAGE_PID_CENTER_POINT_OFFSET), \
        HID_LOGICAL_MIN_N(-USB_AXIS_MAX_ABSOLUTE, 2), \
        HID_LOGICAL_MAX_N(USB_AXIS_MAX_ABSOLUTE, 2), \
        HID_PHYSICAL_MIN_N(-10000, 2), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Positive/Negative Coefficient */ \
        HID_USAGE(HID_USAGE_PID_POSITIVE_COEFFICIENT), \
        HID_USAGE(HID_USAGE_PID_NEGATIVE_COEFFICIENT), \
        HID_LOGICAL_MIN_N(-10000, 2), \
        HID_LOGICAL_MAX_N(10000, 2), \
        HID_PHYSICAL_MIN_N(-10000, 2), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(2), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Positive/Negative Saturation */ \
        HID_USAGE(HID_USAGE_PID_POSITIVE_SATURATION), \
        HID_USAGE(HID_USAGE_PID_NEGATIVE_SATURATION), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(10000, 2), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(2), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Dead Band */ \
        HID_USAGE(HID_USAGE_PID_DEAD_BAND), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(USB_AXIS_MAX_ABSOLUTE, 2), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Set Periodic - set params for periodic wave effects
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_SET_PERIODIC \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_SET_PERIODIC_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_SET_PERIODIC_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Magnitude */ \
        HID_USAGE(HID_USAGE_PID_MAGNITUDE), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(10000, 2), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Offset */ \
        HID_USAGE(HID_USAGE_PID_OFFSET), \
        HID_LOGICAL_MIN_N(-10000, 2), \
        HID_LOGICAL_MAX_N(10000, 2), \
        HID_PHYSICAL_MIN_N(-10000, 2), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Phase */ \
        HID_USAGE(HID_USAGE_PID_PHASE), \
        HID_UNIT(20), \
        HID_UNIT_EXPONENT(0xFE), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(35999, 3), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(35999, 3), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Period */ \
        HID_USAGE(HID_USAGE_PID_PERIOD), \
        HID_UNIT_N(HID_UNIT_SECONDS, 2), \
        HID_UNIT_EXPONENT(0xFD), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(0xFFFE, 3), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(0xFFFE, 3), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        HID_UNIT(0), \
        HID_UNIT_EXPONENT(0), \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Set Constant - describe a constant force
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_SET_CONSTANT \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_SET_CONSTANT_FORCE_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_SET_CONSTANT_FORCE_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Magnitude */ \
        HID_USAGE(HID_USAGE_PID_MAGNITUDE), \
        HID_LOGICAL_MIN_N(-10000, 2), \
        HID_LOGICAL_MAX_N(10000, 2), \
        HID_PHYSICAL_MIN_N(-10000, 2), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Set Ramp - describe a ramp force
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_SET_RAMP \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_SET_RAMP_FORCE_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_SET_RAMP_FROCE_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Ramp Start and End */ \
        HID_USAGE(HID_USAGE_PID_RAMP_START), \
        HID_USAGE(HID_USAGE_PID_RAMP_END), \
        HID_LOGICAL_MIN_N(-10000, 2), \
        HID_LOGICAL_MAX_N(10000, 2), \
        HID_PHYSICAL_MIN_N(-10000, 2), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(2), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Effect Operation - stop/start effects
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_EFFECT_OPERATION \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_EFFECT_OPERATION_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_EFFECT_OPERATION_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Effect Operation */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_OPERATION), \
        HID_COLLECTION(HID_COLLECTION_LOGICAL), \
            HID_USAGE(HID_USAGE_PID_OP_EFFECT_START), \
            HID_USAGE(HID_USAGE_PID_OP_EFFECT_START_SOLO), \
            HID_USAGE(HID_USAGE_PID_OP_EFFECT_STOP), \
            HID_LOGICAL_MIN(1), \
            HID_LOGICAL_MAX(3), \
            HID_REPORT_SIZE(8), \
            HID_REPORT_COUNT(1), \
            HID_OUTPUT(HID_DATA | HID_ARRAY | HID_ABSOLUTE), \
        HID_COLLECTION_END, \
        \
        /* Loop Count */ \
        HID_USAGE(HID_USAGE_PID_LOOP_COUNT), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(255, 2), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(255, 2), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Block Free - delete an effect and free its space
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_BLOCK_FREE \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_FREE_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_BLOCK_FREE_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Device Control - commands affecting entire device
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_DEVICE_CONTROL \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_DEVICE_CONTROL_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_USAGE(HID_USAGE_PID_DEVICE_CONTROL), \
            HID_COLLECTION(HID_COLLECTION_LOGICAL), \
                HID_REPORT_ID(REPORT_ID_DEVICE_CONTROL_REPORT) \
                \
                HID_USAGE(HID_USAGE_PID_DC_ENABLE_ACTUATORS), \
                HID_USAGE(HID_USAGE_PID_DC_DISABLE_ACTUATORS), \
                HID_USAGE(HID_USAGE_PID_DC_STOP_ALL_EFFECTS), \
                HID_USAGE(HID_USAGE_PID_DC_RESET), \
                HID_USAGE(HID_USAGE_PID_DC_PAUSE), \
                HID_USAGE(HID_USAGE_PID_DC_CONTINUE), \
                HID_LOGICAL_MIN(1), \
                HID_LOGICAL_MAX(6), \
                HID_REPORT_SIZE(8), \
                HID_REPORT_COUNT(1), \
                HID_OUTPUT(HID_DATA | HID_ARRAY | HID_ABSOLUTE), \
                \
        HID_COLLECTION_END, \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Output Report: Device Gain - gain across all effects
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_OUTPUT_DEVICE_GAIN \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_DEVICE_GAIN_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_DEVICE_GAIN_REPORT) \
        \
        /* Device Gain */ \
        HID_USAGE(HID_USAGE_PID_DEVICE_GAIN), \
        HID_LOGICAL_MIN(0), \
        HID_LOGICAL_MAX_N(255, 2), \
        HID_PHYSICAL_MIN(0), \
        HID_PHYSICAL_MAX_N(10000, 2), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_OUTPUT(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Feature Report 1: Create New Effect (in device memory)
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_FEATURE_CREATE_NEW_EFFECT \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_CREATE_NEW_EFFECT_PARAM_BLOCK_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_CREATE_NEW_EFFECT_REPORT) \
        \
        /* Effect Type */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_TYPE), \
        HID_COLLECTION(HID_COLLECTION_LOGICAL), \
            HID_USAGE(HID_USAGE_PID_ET_CONSTANT_FORCE), \
            HID_USAGE(HID_USAGE_PID_ET_RAMP), \
            HID_USAGE(HID_USAGE_PID_ET_SQUARE), \
            HID_USAGE(HID_USAGE_PID_ET_SINE), \
            HID_USAGE(HID_USAGE_PID_ET_TRIANGLE), \
            HID_USAGE(HID_USAGE_PID_ET_SAWTOOTH_UP), \
            HID_USAGE(HID_USAGE_PID_ET_SAWTOOTH_DOWN), \
            HID_USAGE(HID_USAGE_PID_ET_SPRING), \
            HID_USAGE(HID_USAGE_PID_ET_DAMPER), \
            HID_USAGE(HID_USAGE_PID_ET_INERTIA), \
            HID_USAGE(HID_USAGE_PID_ET_FRICTION), \
            /* HID_USAGE(HID_USAGE_PID_ET_CUSTOM_FORCE), */ \
            HID_LOGICAL_MIN(1), \
            HID_LOGICAL_MAX(NUM_SUPPORTED_EFFECTS), \
            HID_PHYSICAL_MIN(1), \
            HID_PHYSICAL_MAX(NUM_SUPPORTED_EFFECTS), \
            HID_REPORT_SIZE(8), \
            HID_REPORT_COUNT(1), \
            HID_FEATURE(HID_DATA | HID_ARRAY | HID_ABSOLUTE), \
        HID_COLLECTION_END, \
        \
        /* Byte Count from FF2 descriptor omitted, since we don't support custom forces. */ \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Feature Report 2: Effect Block Load Report - was effect creation successful?
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_FEATURE_BLOCK_LOAD \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_LOAD_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_BLOCK_LOAD_REPORT) \
        \
        /* Block Index */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX), \
        HID_LOGICAL_MIN(1), \
        HID_LOGICAL_MAX(MAX_EFFECTS), \
        HID_PHYSICAL_MIN(1), \
        HID_PHYSICAL_MAX(MAX_EFFECTS), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Block Load Status */ \
        HID_USAGE(HID_USAGE_PID_EFFECT_PARAM_BLOCK_LOAD_STATUS), \
        HID_COLLECTION(HID_COLLECTION_LOGICAL), \
            HID_USAGE(HID_USAGE_PID_BLOCK_LOAD_SUCCESS), \
            HID_USAGE(HID_USAGE_PID_BLOCK_LOAD_FULL), \
            HID_USAGE(HID_USAGE_PID_BLOCK_LOAD_ERROR), \
            HID_LOGICAL_MIN(1), \
            HID_LOGICAL_MAX(3), \
            HID_PHYSICAL_MIN(1), \
            HID_PHYSICAL_MAX(3), \
            HID_REPORT_SIZE(8), \
            HID_REPORT_COUNT(1), \
            HID_FEATURE(HID_DATA | HID_ARRAY | HID_ABSOLUTE), \
        HID_COLLECTION_END, \
        \
        /* RAM Pool Available */ \
        HID_USAGE(HID_USAGE_PID_RAM_POOL_AVAILABLE), \
        HID_LOGICAL_MIN(0), \
        HID_PHYSICAL_MIN(0), \
        HID_LOGICAL_MAX_N(0xFFFF, 3), \
        HID_PHYSICAL_MAX_N(0xFFFF, 3), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_FEATURE(HID_DATA | HID_ARRAY | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END


/////////////////////////////////////////////////////////////////////
// Feature Report 3: Pool size, max simultaneous effects, etc.
/////////////////////////////////////////////////////////////////////

#define FFB_REPORT_DESC_FEATURE_POOL_REPORT \
    HID_USAGE_PAGE(HID_USAGE_PAGE_PID), \
    HID_USAGE(HID_USAGE_PID_PARAM_BLOCK_POOLS_REPORT), \
    HID_COLLECTION(HID_COLLECTION_LOGICAL), \
        HID_REPORT_ID(REPORT_ID_POOL_REPORT) \
        \
        /* RAM Pool Size */ \
        HID_USAGE(HID_USAGE_PID_RAM_POOL_SIZE), \
        HID_REPORT_SIZE(16), \
        HID_REPORT_COUNT(1), \
        HID_LOGICAL_MIN(0), \
        HID_PHYSICAL_MIN(0), \
        HID_LOGICAL_MAX_N(0xFFFF, 3), \
        HID_PHYSICAL_MAX_N(0xFFFF, 3), \
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Simultaneous Maximum Effects */ \
        HID_USAGE(HID_USAGE_PID_SIMULTANEOUS_EFFECTS_MAX), \
        HID_LOGICAL_MAX_N(255, 2), \
        HID_PHYSICAL_MAX_N(255, 2), \
        HID_REPORT_SIZE(8), \
        HID_REPORT_COUNT(1), \
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* Managed Pool, Shared Blocks */ \
        HID_USAGE(HID_USAGE_PID_DEVICE_MANAGED_POOL), \
        HID_USAGE(HID_USAGE_PID_SHARED_PARAMETER_BLOCKS), \
        HID_LOGICAL_MAX(1), \
        HID_PHYSICAL_MAX(1), \
        HID_REPORT_SIZE(1), \
        HID_REPORT_COUNT(2), \
        HID_FEATURE(HID_DATA | HID_VARIABLE | HID_ABSOLUTE), \
        \
        /* padding */ \
        HID_REPORT_SIZE(6), \
        HID_REPORT_COUNT(1), \
        HID_FEATURE(HID_CONSTANT | HID_VARIABLE | HID_ABSOLUTE), \
        \
    HID_COLLECTION_END

#endif