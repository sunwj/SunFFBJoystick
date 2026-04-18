#ifndef _HID_PID_H_
#define _HID_PID_H_

#ifdef __cplusplus
 extern "C" {
#endif

#define HID_USAGE_PAGE_HAPTICS 0x0e
#define HID_USAGE_SIMPLE_HAPTIC_CONTROLLER  0x01

//--------------------------------------------------------------------+
// REPORT DESCRIPTOR
//--------------------------------------------------------------------+

//------------- ITEM & TAG -------------//
#define HID_REPORT_DATA_0(data)
#define HID_REPORT_DATA_1(data) , data
#define HID_REPORT_DATA_2(data) , U16_TO_U8S_LE(data)
#define HID_REPORT_DATA_3(data) , U32_TO_U8S_LE(data)

#define HID_REPORT_ITEM(data, tag, type, size) \
  (((tag) << 4) | ((type) << 2) | (size)) HID_REPORT_DATA_##size(data)

#define HID_OUTPUT_N(x, n)     HID_REPORT_ITEM(x, RI_MAIN_OUTPUT        , RI_TYPE_MAIN, n)

#define HID_INPUT(x)           HID_REPORT_ITEM(x, RI_MAIN_INPUT         , RI_TYPE_MAIN, 1)
#define HID_OUTPUT(x)          HID_REPORT_ITEM(x, RI_MAIN_OUTPUT        , RI_TYPE_MAIN, 1)
#define HID_COLLECTION(x)      HID_REPORT_ITEM(x, RI_MAIN_COLLECTION    , RI_TYPE_MAIN, 1)
#define HID_FEATURE(x)         HID_REPORT_ITEM(x, RI_MAIN_FEATURE       , RI_TYPE_MAIN, 1)
#define HID_COLLECTION_END     HID_REPORT_ITEM(x, RI_MAIN_COLLECTION_END, RI_TYPE_MAIN, 0)

#define HID_DATA             (0<<0)
#define HID_CONSTANT         (1<<0)

#define HID_ARRAY            (0<<1)
#define HID_VARIABLE         (1<<1)

#define HID_ABSOLUTE         (0<<2)
#define HID_RELATIVE         (1<<2)

#define HID_WRAP_NO          (0<<3)
#define HID_WRAP             (1<<3)

#define HID_LINEAR           (0<<4)
#define HID_NONLINEAR        (1<<4)

#define HID_PREFERRED_STATE  (0<<5)
#define HID_PREFERRED_NO     (1<<5)

#define HID_NO_NULL_POSITION (0<<6)
#define HID_NULL_STATE       (1<<6)

#define HID_NON_VOLATILE     (0<<7)
#define HID_VOLATILE         (1<<7)

#define HID_BITFIELD         (0<<8)
#define HID_BUFFERED_BYTES   (1<<8)

#define HID_USAGE_PAGE(x)         HID_REPORT_ITEM(x, RI_GLOBAL_USAGE_PAGE, RI_TYPE_GLOBAL, 1)
#define HID_USAGE_PAGE_N(x, n)    HID_REPORT_ITEM(x, RI_GLOBAL_USAGE_PAGE, RI_TYPE_GLOBAL, n)

#define HID_LOGICAL_MIN(x)        HID_REPORT_ITEM(x, RI_GLOBAL_LOGICAL_MIN, RI_TYPE_GLOBAL, 1)
#define HID_LOGICAL_MIN_N(x, n)   HID_REPORT_ITEM(x, RI_GLOBAL_LOGICAL_MIN, RI_TYPE_GLOBAL, n)

#define HID_LOGICAL_MAX(x)        HID_REPORT_ITEM(x, RI_GLOBAL_LOGICAL_MAX, RI_TYPE_GLOBAL, 1)
#define HID_LOGICAL_MAX_N(x, n)   HID_REPORT_ITEM(x, RI_GLOBAL_LOGICAL_MAX, RI_TYPE_GLOBAL, n)

#define HID_PHYSICAL_MIN(x)       HID_REPORT_ITEM(x, RI_GLOBAL_PHYSICAL_MIN, RI_TYPE_GLOBAL, 1)
#define HID_PHYSICAL_MIN_N(x, n)  HID_REPORT_ITEM(x, RI_GLOBAL_PHYSICAL_MIN, RI_TYPE_GLOBAL, n)

#define HID_PHYSICAL_MAX(x)       HID_REPORT_ITEM(x, RI_GLOBAL_PHYSICAL_MAX, RI_TYPE_GLOBAL, 1)
#define HID_PHYSICAL_MAX_N(x, n)  HID_REPORT_ITEM(x, RI_GLOBAL_PHYSICAL_MAX, RI_TYPE_GLOBAL, n)

#define HID_UNIT_EXPONENT(x)      HID_REPORT_ITEM(x, RI_GLOBAL_UNIT_EXPONENT, RI_TYPE_GLOBAL, 1)
#define HID_UNIT_EXPONENT_N(x, n) HID_REPORT_ITEM(x, RI_GLOBAL_UNIT_EXPONENT, RI_TYPE_GLOBAL, n)

#define HID_UNIT(x)               HID_REPORT_ITEM(x, RI_GLOBAL_UNIT, RI_TYPE_GLOBAL, 1)
#define HID_UNIT_N(x, n)          HID_REPORT_ITEM(x, RI_GLOBAL_UNIT, RI_TYPE_GLOBAL, n)

#define HID_REPORT_SIZE(x)        HID_REPORT_ITEM(x, RI_GLOBAL_REPORT_SIZE, RI_TYPE_GLOBAL, 1)
#define HID_REPORT_SIZE_N(x, n)   HID_REPORT_ITEM(x, RI_GLOBAL_REPORT_SIZE, RI_TYPE_GLOBAL, n)

#define HID_REPORT_ID(x)          HID_REPORT_ITEM(x, RI_GLOBAL_REPORT_ID, RI_TYPE_GLOBAL, 1),
#define HID_REPORT_ID_N(x, n)     HID_REPORT_ITEM(x, RI_GLOBAL_REPORT_ID, RI_TYPE_GLOBAL, n),

#define HID_REPORT_COUNT(x)       HID_REPORT_ITEM(x, RI_GLOBAL_REPORT_COUNT, RI_TYPE_GLOBAL, 1)
#define HID_REPORT_COUNT_N(x, n)  HID_REPORT_ITEM(x, RI_GLOBAL_REPORT_COUNT, RI_TYPE_GLOBAL, n)

#define HID_PUSH                  HID_REPORT_ITEM(x, RI_GLOBAL_PUSH, RI_TYPE_GLOBAL, 0)
#define HID_POP                   HID_REPORT_ITEM(x, RI_GLOBAL_POP, RI_TYPE_GLOBAL, 0)

#define HID_USAGE(x)              HID_REPORT_ITEM(x, RI_LOCAL_USAGE, RI_TYPE_LOCAL, 1)
#define HID_USAGE_N(x, n)         HID_REPORT_ITEM(x, RI_LOCAL_USAGE, RI_TYPE_LOCAL, n)

#define HID_USAGE_MIN(x)          HID_REPORT_ITEM(x, RI_LOCAL_USAGE_MIN, RI_TYPE_LOCAL, 1)
#define HID_USAGE_MIN_N(x, n)     HID_REPORT_ITEM(x, RI_LOCAL_USAGE_MIN, RI_TYPE_LOCAL, n)

#define HID_USAGE_MAX(x)          HID_REPORT_ITEM(x, RI_LOCAL_USAGE_MAX, RI_TYPE_LOCAL, 1)
#define HID_USAGE_MAX_N(x, n)     HID_REPORT_ITEM(x, RI_LOCAL_USAGE_MAX, RI_TYPE_LOCAL, n)

//--------------------------------------------------------------------+
// Usage Table
/* Usage Types Data
    Sel  Selector               Array
    SV   Static Value           Constant, Variable, Absolute
    SF   Static Flag            Constant, Variable, Absolute
    DV   Dynamic Value          Constant, Variable, Absolute
    DF   Dynamic Flag           Constant, Variable, Absolute
*/
/* Usage Types Collection
    NAry  Named Array             Logical
    CA    Collection Application  Application
    CL    Collection Logical      Logical
    CP    Collection Physical     Physical
    US    Usage Switch            Logical
    UM    Usage Modifier          Logical
*/
//--------------------------------------------------------------------+

/// HID Usage Table: Physical Input Device Page (0x0F)
enum {
  HID_USAGE_PID_UNDEFINED                                = 0x00,
  HID_USAGE_PID_PHYSICAL_INPUT_DEVICE                    = 0x01,
  HID_USAGE_PID_NORMAL                                   = 0x20,
  HID_USAGE_PID_SET_EFFECT_REPORT                        = 0x21,
  HID_USAGE_PID_EFFECT_PARAMETER_BLOCK_INDEX             = 0x22,
  HID_USAGE_PID_PARAMETER_BLOCK_OFFSET                   = 0x23,
  HID_USAGE_PID_ROM_FLAG                                 = 0x24,
  HID_USAGE_PID_EFFECT_TYPE                              = 0x25,
  HID_USAGE_PID_ET_CONSTANTFORCE                         = 0x26,
  HID_USAGE_PID_ET_RAMP                                  = 0x27,
  HID_USAGE_PID_ET_CUSTOMFORCE                           = 0x28,
  HID_USAGE_PID_ET_SQUARE                                = 0x30,
  HID_USAGE_PID_ET_SINE                                  = 0x31,
  HID_USAGE_PID_ET_TRIANGLE                              = 0x32,
  HID_USAGE_PID_ET_SAWTOOTH_UP                           = 0x33,
  HID_USAGE_PID_ET_SAWTOOTH_DOWN                         = 0x34,
  HID_USAGE_PID_ET_SPRING                                = 0x40,
  HID_USAGE_PID_ET_DAMPER                                = 0x41,
  HID_USAGE_PID_ET_INERTIA                               = 0x42,
  HID_USAGE_PID_ET_FRICTION                              = 0x43,
  HID_USAGE_PID_DURATION                                 = 0x50,
  HID_USAGE_PID_SAMPLE_PERIOD                            = 0x51,
  HID_USAGE_PID_GAIN                                     = 0x52,
  HID_USAGE_PID_TRIGGER_BUTTON                           = 0x53,
  HID_USAGE_PID_TRIGGER_REPEAT_INTERVAL                  = 0x54,
  HID_USAGE_PID_AXES_ENABLE                              = 0x55,
  HID_USAGE_PID_DIRECTION_ENABLE                         = 0x56,
  HID_USAGE_PID_DIRECTION                                = 0x57,
  HID_USAGE_PID_TYPE_SPECIFIC_BLOCK_OFFSET               = 0x58,
  HID_USAGE_PID_BLOCK_TYPE                               = 0x59,
  HID_USAGE_PID_SET_ENVELOPE_REPORT                      = 0x5a,
  HID_USAGE_PID_ATTACK_LEVEL                             = 0x5b,
  HID_USAGE_PID_ATTACK_TIME                              = 0x5c,
  HID_USAGE_PID_FADE_LEVEL                               = 0x5d,
  HID_USAGE_PID_FADE_TIME                                = 0x5e,
  HID_USAGE_PID_SET_CONDITION_REPORT                     = 0x5f,
  HID_USAGE_PID_CENTERPOINT_OFFSET                       = 0x60,
  HID_USAGE_PID_POSITIVE_COEFFICIENT                     = 0x61,
  HID_USAGE_PID_NEGATIVE_COEFFICIENT                     = 0x62,
  HID_USAGE_PID_POSITIVE_SATURATION                      = 0x63,
  HID_USAGE_PID_NEGATIVE_SATURATION                      = 0x64,
  HID_USAGE_PID_DEAD_BAND                                = 0x65,
  HID_USAGE_PID_DOWNLOAD_FORCE_SAMPLE                    = 0x66,
  HID_USAGE_PID_ISOCH_CUSTOMFORCE_ENABLE                 = 0x67,
  HID_USAGE_PID_CUSTOMFORCE_DATA_REPORT                  = 0x68,
  HID_USAGE_PID_CUSTOMFORCE_DATA                         = 0x69,
  HID_USAGE_PID_CUSTOMFORCE_VENDOR_DEFINED_DATA          = 0x6a,
  HID_USAGE_PID_SET_CUSTOMFORCE_REPORT                   = 0x6b,
  HID_USAGE_PID_CUSTOMFORCE_DATA_OFFSET                  = 0x6c,
  HID_USAGE_PID_SAMPLE_COUNT                             = 0x6d,
  HID_USAGE_PID_SET_PERIODIC_REPORT                      = 0x6e,
  HID_USAGE_PID_OFFSET                                   = 0x6f,
  HID_USAGE_PID_MAGNITUDE                                = 0x70,
  HID_USAGE_PID_PHASE                                    = 0x71,
  HID_USAGE_PID_PERIOD                                   = 0x72,
  HID_USAGE_PID_SET_CONSTANTFORCE_REPORT                 = 0x73,
  HID_USAGE_PID_SET_RAMPFORCE_REPORT                     = 0x74,
  HID_USAGE_PID_RAMP_START                               = 0x75,
  HID_USAGE_PID_RAMP_END                                 = 0x76,
  HID_USAGE_PID_EFFECT_OPERATION_REPORT                  = 0x77,
  HID_USAGE_PID_EFFECT_OPERATION                         = 0x78,
  HID_USAGE_PID_OP_EFFECT_START                          = 0x79,
  HID_USAGE_PID_OP_EFFECT_START_SOLO                     = 0x7a,
  HID_USAGE_PID_OP_EFFECT_STOP                           = 0x7b,
  HID_USAGE_PID_LOOP_COUNT                               = 0x7c,
  HID_USAGE_PID_DEVICE_GAIN_REPORT                       = 0x7d,
  HID_USAGE_PID_DEVICE_GAIN                              = 0x7e,
  HID_USAGE_PID_PARAMETER_BLOCK_POOLS_REPORT             = 0x7f,
  HID_USAGE_PID_RAM_POOL_SIZE                            = 0x80,
  HID_USAGE_PID_ROM_POOL_SIZE                            = 0x81,
  HID_USAGE_PID_ROM_EFFECT_BLOCK_COUNT                   = 0x82,
  HID_USAGE_PID_SIMULTANEOUS_EFFECTS_MAX                 = 0x83,
  HID_USAGE_PID_POOL_ALIGNMENT                           = 0x84,
  HID_USAGE_PID_PARAMETER_BLOCK_MOVE_REPORT              = 0x85,
  HID_USAGE_PID_MOVE_SOURCE                              = 0x86,
  HID_USAGE_PID_MOVE_DESTINATION                         = 0x87,
  HID_USAGE_PID_MOVE_LENGTH                              = 0x88,
  HID_USAGE_PID_EFFECT_PARAMETER_BLOCK_LOAD_REPORT       = 0x89,
  HID_USAGE_PID_EFFECT_PARAMETER_BLOCK_LOAD_STATUS       = 0x8b,
  HID_USAGE_PID_BLOCK_LOAD_SUCCESS                       = 0x8c,
  HID_USAGE_PID_BLOCK_LOAD_FULL                          = 0x8d,
  HID_USAGE_PID_BLOCK_LOAD_ERROR                         = 0x8e,
  HID_USAGE_PID_BLOCK_HANDLE                             = 0x8f,
  HID_USAGE_PID_EFFECT_PARAMETER_BLOCK_FREE_REPORT       = 0x90,
  HID_USAGE_PID_TYPE_SPECIFIC_BLOCK_HANDLE               = 0x91,
  HID_USAGE_PID_PID_STATE_REPORT                         = 0x92,
  HID_USAGE_PID_EFFECT_PLAYING                           = 0x94,
  HID_USAGE_PID_PID_DEVICE_CONTROL_REPORT                = 0x95,
  HID_USAGE_PID_PID_DEVICE_CONTROL                       = 0x96,
  HID_USAGE_PID_DC_ENABLE_ACTUATORS                      = 0x97,
  HID_USAGE_PID_DC_DISABLE_ACTUATORS                     = 0x98,
  HID_USAGE_PID_DC_STOP_ALL_EFFECTS                      = 0x99,
  HID_USAGE_PID_DC_RESET                                 = 0x9a,
  HID_USAGE_PID_DC_PAUSE                                 = 0x9b,
  HID_USAGE_PID_DC_CONTINUE                              = 0x9c,
  HID_USAGE_PID_DEVICE_PAUSED                            = 0x9f,
  HID_USAGE_PID_ACTUATORS_ENABLED                        = 0xa0,
  HID_USAGE_PID_SAFETY_SWITCH                            = 0xa4,
  HID_USAGE_PID_ACTUATOR_OVERRIDE_SWITCH                 = 0xa5,
  HID_USAGE_PID_ACTUATOR_POWER                           = 0xa6,
  HID_USAGE_PID_START_DELAY                              = 0xa7,
  HID_USAGE_PID_PARAMETER_BLOCK_SIZE                     = 0xa8,
  HID_USAGE_PID_DEVICEMANAGED_POOL                       = 0xa9,
  HID_USAGE_PID_SHARED_PARAMETER_BLOCKS                  = 0xaa,
  HID_USAGE_PID_CREATE_NEW_EFFECT_PARAMETER_BLOCK_REPORT = 0xab,
  HID_USAGE_PID_RAM_POOL_AVAILABLE                       = 0xac,
};

#ifdef __cplusplus
}
#endif

#endif