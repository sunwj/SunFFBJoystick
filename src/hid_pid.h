#ifndef _HID_PID_H_
#define _HID_PID_H_

#define HID_USAGE_PAGE_HAPTICS 0x0e
#define HID_USAGE_SIMPLE_HAPTIC_CONTROLLER  0x01
#define HID_OUTPUT_N(x, n)     HID_REPORT_ITEM(x, RI_MAIN_OUTPUT        , RI_TYPE_MAIN, n)

// PID Usage Page
enum
{
    HID_USAGE_PID_SET_EFFECT_REPORT             = 0x21,
    HID_USAGE_PID_EFFECT_PARAM_BLOCK_INDEX      = 0x22,
    HID_USAGE_PID_EFFECT_PARAM_BLOCK_OFFSET     = 0x23,

    HID_USAGE_PID_EFFECT_TYPE                   = 0x25,
    HID_USAGE_PID_ET_CONSTANT_FORCE             = 0x26,
    HID_USAGE_PID_ET_RAMP                       = 0x27,
    HID_USAGE_PID_ET_CUSTOM_FORCE               = 0x28,
    HID_USAGE_PID_ET_SQUARE                     = 0x30,
    HID_USAGE_PID_ET_SINE                       = 0x31,
    HID_USAGE_PID_ET_TRIANGLE                   = 0x32,
    HID_USAGE_PID_ET_SAWTOOTH_UP                = 0x33,
    HID_USAGE_PID_ET_SAWTOOTH_DOWN              = 0x34,
    HID_USAGE_PID_ET_SPRING                     = 0x40,
    HID_USAGE_PID_ET_DAMPER                     = 0x41,
    HID_USAGE_PID_ET_INERTIA                    = 0x42,
    HID_USAGE_PID_ET_FRICTION                   = 0x43,

    HID_USAGE_PID_DURATION                      = 0x50,
    HID_USAGE_PID_SAMPLE_PERIOD                 = 0x51,
    HID_USAGE_PID_GAIN                          = 0x52,
    HID_USAGE_PID_TRIGGER_BUTTON                = 0x53,
    HID_USAGE_PID_TRIGGER_REPEAT_INTERVAL       = 0x54,
    HID_USAGE_PID_AXES_ENABLE                   = 0x55,
    HID_USAGE_PID_DIRECTION_ENABLE              = 0x56,
    HID_USAGE_PID_DIRECTION                     = 0x57,
    HID_USAGE_PID_TYPE_SPECIFIC_BLOCK_OFFSET    = 0x58,

    HID_USAGE_PID_SET_ENVELOPE_REPORT           = 0x5a,
    HID_USAGE_PID_ATTACK_LEVEL                  = 0x5b,
    HID_USAGE_PID_ATTACK_TIME                   = 0x5c,
    HID_USAGE_PID_FADE_LEVEL                    = 0x5d,
    HID_USAGE_PID_FADE_TIME                     = 0x5e,

    HID_USAGE_PID_SET_CONDITION_REPORT          = 0x5f,
    HID_USAGE_PID_CENTER_POINT_OFFSET           = 0x60,
    HID_USAGE_PID_POSITIVE_COEFFICIENT          = 0x61,
    HID_USAGE_PID_NEGATIVE_COEFFICIENT          = 0x62,
    HID_USAGE_PID_POSITIVE_SATURATION           = 0x63,
    HID_USAGE_PID_NEGATIVE_SATURATION           = 0x64,
    HID_USAGE_PID_DEAD_BAND                     = 0x65,

    HID_USAGE_PID_DOWNLOAD_FORCE_SAMPLE         = 0x66,

    HID_USAGE_PID_CUSTOM_FORCE_DATA_REPORT          = 0x68,
    HID_USAGE_PID_CUSTOM_FORCE_DATA                 = 0x69,
    HID_USAGE_PID_CUSTOM_FORCE_VENDOR_DEFINED_DATA  = 0x6a,

    HID_USAGE_PID_SET_CUSTOM_FORCE_REPORT       = 0x6b,
    HID_USAGE_PID_CUSTOM_FORCE_DATA_OFFSET      = 0x6c,
    HID_USAGE_PID_SAMPLE_COUNT                  = 0x6d,

    HID_USAGE_PID_SET_PERIODIC_REPORT           = 0x6e,
    HID_USAGE_PID_OFFSET                        = 0x6f,
    HID_USAGE_PID_MAGNITUDE                     = 0x70,
    HID_USAGE_PID_PHASE                         = 0x71,
    HID_USAGE_PID_PERIOD                        = 0x72,

    HID_USAGE_PID_SET_CONSTANT_FORCE_REPORT     = 0x73,

    HID_USAGE_PID_SET_RAMP_FORCE_REPORT         = 0x74,
    HID_USAGE_PID_RAMP_START                    = 0x75,
    HID_USAGE_PID_RAMP_END                      = 0x76,

    HID_USAGE_PID_EFFECT_OPERATION_REPORT       = 0x77,

    HID_USAGE_PID_EFFECT_OPERATION              = 0x78,
    HID_USAGE_PID_OP_EFFECT_START               = 0x79,
    HID_USAGE_PID_OP_EFFECT_START_SOLO          = 0x7a,
    HID_USAGE_PID_OP_EFFECT_STOP                = 0x7b,
    HID_USAGE_PID_LOOP_COUNT                    = 0x7c,

    HID_USAGE_PID_DEVICE_GAIN_REPORT            = 0x7d,
    HID_USAGE_PID_DEVICE_GAIN                   = 0x7e,

    HID_USAGE_PID_PARAM_BLOCK_POOLS_REPORT      = 0x7f,
    HID_USAGE_PID_RAM_POOL_SIZE                 = 0x80,
    HID_USAGE_PID_ROM_POOL_SIZE                 = 0x81,
    HID_USAGE_PID_ROM_EFFECT_BLOCK_COUNT        = 0x82,
    HID_USAGE_PID_SIMULTANEOUS_EFFECTS_MAX      = 0x83,
    HID_USAGE_PID_POOL_ALIGNMENT                = 0x84,

    HID_USAGE_PID_EFFECT_PARAM_BLOCK_LOAD_REPORT    = 0x89,
    HID_USAGE_PID_EFFECT_PARAM_BLOCK_LOAD_STATUS    = 0x8b,
    HID_USAGE_PID_BLOCK_LOAD_SUCCESS                = 0x8c,
    HID_USAGE_PID_BLOCK_LOAD_FULL                   = 0x8d,
    HID_USAGE_PID_BLOCK_LOAD_ERROR                  = 0x8e,
    HID_USAGE_PID_BLOCK_HANDLE                      = 0x8f,
    HID_USAGE_PID_EFFECT_PARAM_BLOCK_FREE_REPORT    = 0x90,

    HID_USAGE_PID_STATE_REPORT                  = 0x92,
    HID_USAGE_PID_EFFECT_PLAYING                = 0x94,

    HID_USAGE_PID_DEVICE_CONTROL_REPORT         = 0x95,
    HID_USAGE_PID_DEVICE_CONTROL                = 0x96,
    HID_USAGE_PID_DC_ENABLE_ACTUATORS           = 0x97,
    HID_USAGE_PID_DC_DISABLE_ACTUATORS          = 0x98,
    HID_USAGE_PID_DC_STOP_ALL_EFFECTS           = 0x99,
    HID_USAGE_PID_DC_RESET                      = 0x9a,
    HID_USAGE_PID_DC_PAUSE                      = 0x9b,
    HID_USAGE_PID_DC_CONTINUE                   = 0x9c,

    HID_USAGE_PID_DEVICE_PAUSED                 = 0x9f,
    HID_USAGE_PID_ACTUATORS_ENABLED             = 0xa0,
    HID_USAGE_PID_SAFETY_SWITCH                 = 0xa4,
    HID_USAGE_PID_ACTUATOR_OVERRIDE_SWITCH      = 0xa5,
    HID_USAGE_PID_ACTUATOR_POWER                = 0xa6,
    HID_USAGE_PID_START_DELAY                   = 0xa7,

    HID_USAGE_PID_PARAMETER_BLOCK_SIZE          = 0xa8,
    HID_USAGE_PID_DEVICE_MANAGED_POOL           = 0xa9,
    HID_USAGE_PID_SHARED_PARAMETER_BLOCKS       = 0xaa,

    HID_USAGE_CREATE_NEW_EFFECT_PARAM_BLOCK_REPORT  = 0xab,
    HID_USAGE_PID_RAM_POOL_AVAILABLE                = 0xac,
};

#endif