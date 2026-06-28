#ifndef _FFB_FORCE_CALCULATOR_H_
#define _FFB_FORCE_CALCULATOR_H_

#include "ffb_report_types.h"
#include "ffb_report_handler.h"
#include "ffb_device_input.h"

namespace SunFFB
{
    // Pure computation: iterates effect blocks, computes output forces per axis.
    // Stateless — all mutable state lives in FFBReportHandler and FFBDeviceInput.
    class FFBForceCalculator
    {
        public:
        // Compute summed forces for all axes from active effects.
        // Reads handler (effect config + PID state) and device input (axis metrics).
        // Writes forces[NUM_AXIS] clamped to [-USB_MAX_MAGNITUDE, +USB_MAX_MAGNITUDE].
        void force_calculator(FFBReportHandler& ffbReportHandler, const FFBDeviceInput& ffbDeviceInput, int32_t forces[NUM_AXIS]) const;

        private:
        // Constant force: return the magnitude from type-specific data.
        float constant_force_calculator(const EffectBlock& effectBlock) const;
        // Ramp force: linear interpolation from rampStart to rampEnd over duration.
        float ramp_force_calculator(const EffectBlock& effectBlock, float elapsedTime) const;
        // Periodic force: square/sine/triangle/sawtooth waveform generation.
        float periodic_force_calculator(uint8_t effectType, const EffectBlock& effectBlock, uint32_t elapsedTime) const;
        // Condition force: per-axis spring/damper/inertia/friction computation.
        void condition_force_calculator(const EffectBlock& effectBlock, const float metrics[NUM_AXIS], const float maxMetrics[NUM_AXIS], float forces[NUM_AXIS]) const;

        // Envelope multiplier: attack/fade curve relative to baseMagnitude.
        float get_envelope(const SetEnvelopeReportData& envelopeData, uint32_t elapsedTime, uint16_t duration, float baseMagnitude) const;
        // Extract the base force magnitude from type-specific data (for envelope scaling).
        float get_base_magnitude(const EffectBlock& effectBlock, uint8_t effectType) const;
        // Apply a single axis of condition: deadband → offset → coeff → saturation.
        float apply_condition(const SetConditionReportData& conditionData, float metric, float maxMetric) const;
    };

    // Normalize a signed value to [-1, 1] range by dividing by maxVal.
    inline float normalize_range(float x, float maxVal) { return x / maxVal; }
} // namespace SunFFB

#endif