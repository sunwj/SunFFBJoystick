#ifndef SUNFFB_MOTOR_PAYLOAD_H
#define SUNFFB_MOTOR_PAYLOAD_H
#include <stdint.h>
#include "config_ffb.h"
namespace SunFFB {
#pragma pack(push, 1)
struct ForcePayload { int32_t force[NUM_AXIS]; };
struct PositionPayload { uint16_t position[NUM_AXIS]; };
#pragma pack(pop)
}
#endif
