#ifndef _FFB_FORCE_CALCULATOR_H_
#define _FFB_FORCE_CALCULATOR_H_

#include "ffb_report_types.h"
#include "ffb_report_handler.h"
#include "ffb_device_input.h"

namespace SunFFB
{
    class FFBForceCalculator
    {
        public:
        void force_calculator(const FFBReportHandler& ffbReportHandler, const FFBDeviceInput& ffbDeviceInput, int32_t forces[NUM_AXIS]) const;

        private:
        float constant_force_calculator(const EffectBlock& effectBlock) const;
        float ramp_force_calculator(const EffectBlock& effectBlock, float elapsedTime) const;
        float periodic_force_calculator(uint8_t effectType, const EffectBlock& effectBlock, uint32_t elapsedTime) const;
        void condition_force_calculator(const EffectBlock& effectBlock, const int32_t metrics[NUM_AXIS], const int32_t maxMetrics[NUM_AXIS], float forces[NUM_AXIS]) const;

        float get_envelope(const SetEnvelopeReportData& envelopeData, uint32_t elapsedTime, uint16_t duration) const;
        float apply_condition(const SetConditionReportData& conditionData, float metric) const;
        bool is_trigger_playing(EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime) const;
        bool is_effect_playing(const EffectBlock& effectBlock, uint8_t triggerButtonState, uint32_t currentTime) const;
    };

    // template <typename T>
    // inline float normalize_range(T x, T maxVal) { return float(x) / maxVal; }
    inline float normalize_range(float x, float maxVal) { return x / maxVal; }
} // namespace SunFFB

#endif