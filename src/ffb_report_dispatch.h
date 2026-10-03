/**
 * Hardware-independent validation and dispatch for incoming HID commands, exercised by native tests.
 * length excludes the report ID; report type, ID and exact payload size must all match.
 * Copy bytes into local report structures instead of dereferencing unaligned USB buffers.
 */

#pragma once
#include <cstring>
#include "ffb_report_handler.h"

namespace SunFFB
{
    // HID report types: Output = 2, Feature = 3. Kept independent of TinyUSB
    // so validation and dispatch can be exercised by the native test suite.
    inline uint16_t output_report_size(uint8_t id, uint8_t type)
    {
        if (id == REPORT_ID_CREATE_NEW_EFFECT_REPORT)
            return type == 3 ? sizeof(CreateNewEffectReportData) : 0;

        if (type != 2)
            return 0;

        switch (id)
        {
            case REPORT_ID_SET_EFFECT_REPORT:
                return sizeof(SetEffectReportData);
            case REPORT_ID_SET_ENVELOPE_REPORT:
                return sizeof(SetEnvelopeReportData);
            case REPORT_ID_SET_CONDITION_REPORT:
                return sizeof(SetConditionReportData);
            case REPORT_ID_SET_PERIODIC_REPORT:
                return sizeof(SetPeriodicReportData);
            case REPORT_ID_SET_CONSTANT_FORCE_REPORT:
                return sizeof(SetConstantForceReportData);
            case REPORT_ID_SET_RAMP_FORCE_REPORT:
                return sizeof(SetRampForceReportData);
            case REPORT_ID_EFFECT_OPERATION_REPORT:
                return sizeof(EffectOperationReportData);
            case REPORT_ID_BLOCK_FREE_REPORT:
                return sizeof(BlockFreeReportData);
            case REPORT_ID_DEVICE_CONTROL_REPORT:
                return sizeof(DeviceControlReportData);
            case REPORT_ID_DEVICE_GAIN_REPORT:
                return sizeof(DeviceGainReportData);
            default:
                return 0;
        }
    }

    inline bool valid_output_report(uint8_t id, uint8_t type, const uint8_t* data, uint16_t length)
    {
        // Require exact length: short reports risk overreads, while oversized reports hide layout mismatches.
        const uint16_t expected = output_report_size(id, type);
        return data && expected && length == expected;
    }

    inline bool dispatch_output_report(FFBReportHandler& handler, uint8_t id, uint8_t type,
                                       const uint8_t* data, uint16_t length)
    {
        // No locking here: the firmware worker synchronizes access; native tests call this directly.
        if (!valid_output_report(id, type, data, length))
            return false;

        switch (id)
        {
            case REPORT_ID_SET_EFFECT_REPORT:
            {
                SetEffectReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_effect(&report);
                return true;
            }

            case REPORT_ID_SET_ENVELOPE_REPORT:
            {
                SetEnvelopeReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_envelope(&report);
                return true;
            }

            case REPORT_ID_SET_CONDITION_REPORT:
            {
                SetConditionReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_condition(&report);
                return true;
            }

            case REPORT_ID_SET_PERIODIC_REPORT:
            {
                SetPeriodicReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_periodic(&report);
                return true;
            }

            case REPORT_ID_SET_CONSTANT_FORCE_REPORT:
            {
                SetConstantForceReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_constant_force(&report);
                return true;
            }

            case REPORT_ID_SET_RAMP_FORCE_REPORT:
            {
                SetRampForceReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_ramp_force(&report);
                return true;
            }

            case REPORT_ID_EFFECT_OPERATION_REPORT:
            {
                EffectOperationReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_effect_operation(&report);
                return true;
            }

            case REPORT_ID_BLOCK_FREE_REPORT:
            {
                BlockFreeReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_effect_block_free(&report);
                return true;
            }

            case REPORT_ID_DEVICE_CONTROL_REPORT:
            {
                DeviceControlReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_device_control(&report);
                return true;
            }

            case REPORT_ID_DEVICE_GAIN_REPORT:
            {
                DeviceGainReportData report;
                memcpy(&report, data, sizeof(report));
                handler.set_device_gain(&report);
                return true;
            }

            case REPORT_ID_CREATE_NEW_EFFECT_REPORT:
            {
                CreateNewEffectReportData report;
                memcpy(&report, data, sizeof(report));
                handler.create_new_effect(&report);
                return true;
            }

            default:
                return false;
        }
    }
} // namespace SunFFB
