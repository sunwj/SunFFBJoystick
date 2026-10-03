#include <unity.h>
#include <cstdarg>
#include <cstdint>
#include "ffb_force_calculator.h"
#include "ffb_report_handler.h"

using namespace SunFFB;

static uint32_t fakeMillis;
static uint32_t fakeMicros;
extern "C" uint32_t _millis(void)
{
    return fakeMillis;
}

extern "C" uint32_t _micros(void)
{
    return fakeMicros;
}

extern "C" void _debug_printf(const char*, ...)
{
}

struct Fixture
{
    FFBReportHandler handler;
    FFBDeviceInput input;
    FFBForceCalculator calculator;

    Fixture()
    {
        fakeMillis = fakeMicros = 0;
        input.reset();
        DeviceControlReportData reset{4};
        handler.set_device_control(&reset);
    }

    uint8_t create(uint8_t type, uint16_t duration = 1000, uint16_t direction = 0,
                   uint16_t startDelay = 0, uint8_t gain = USB_MAX_EFFECT_GAIN,
                   uint16_t samplePeriod = 0, uint8_t axisEnable = DIRECTION_ENABLE,
                   uint8_t triggerButton = USB_NO_TRIGGER_BUTTON)
    {
        CreateNewEffectReportData request{type};
        handler.create_new_effect(&request);
        uint8_t index = handler.get_block_load_report_data()->effectBlockIndex;
        TEST_ASSERT_NOT_EQUAL_UINT8(0, index);
        SetEffectReportData effect{};
        effect.effectBlockIndex = index;
        effect.effectType = type;
        effect.duration = duration;
        effect.gain = gain;
        effect.samplePeriod = samplePeriod;
        effect.triggerButton = triggerButton;
        effect.axisEnable = axisEnable;
        effect.directions[0] = direction;
        effect.startDelay = startDelay;
        handler.set_effect(&effect);
        return index;
    }

    void start(uint8_t index, uint8_t loops = 1)
    {
        EffectOperationReportData operation{index, 1, loops};
        handler.set_effect_operation(&operation);
    }

    void at(uint32_t ms)
    {
        fakeMillis = ms;
        fakeMicros = ms * 1000;
    }

    void calculate(int32_t (&forces)[NUM_AXIS])
    {
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
            forces[i] = 0;
        calculator.force_calculator(handler, input, forces);
    }
};

static void constant(Fixture& f, uint8_t index, int16_t magnitude)
{
    SetConstantForceReportData data{index, magnitude};
    f.handler.set_constant_force(&data);
}

static void ramp(Fixture& f, uint8_t index, int16_t start, int16_t end)
{
    SetRampForceReportData data{index, start, end};
    f.handler.set_ramp_force(&data);
}

static void periodic(Fixture& f, uint8_t index, uint16_t magnitude, int16_t offset, uint16_t phase,
                     uint16_t period)
{
    SetPeriodicReportData data{index, magnitude, offset, phase, period};
    f.handler.set_periodic(&data);
}

static void condition(Fixture& f, uint8_t index, uint8_t parameterBlockOffset, int16_t cpOffset,
                      int16_t positiveCoefficient, int16_t negativeCoefficient,
                      uint16_t positiveSaturation = 10000, uint16_t negativeSaturation = 10000,
                      uint16_t deadBand = 0)
{
    SetConditionReportData data{index,
                                parameterBlockOffset,
                                cpOffset,
                                positiveCoefficient,
                                negativeCoefficient,
                                positiveSaturation,
                                negativeSaturation,
                                deadBand};
    f.handler.set_condition(&data);
}

static void set_input(Fixture& f, int16_t axis0, int16_t axis1, uint32_t dtUs = 10000)
{
    fakeMicros += dtUs;
    int16_t axes[NUM_AXIS] = {axis0, axis1};
    f.input.update_axis(axes);
}

void setUp()
{
}

void tearDown()
{
}

static void test_constant_force_and_direction()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_CONSTANT);
    constant(f, index, 4000);
    f.start(index);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 0, force[0]);
    TEST_ASSERT_INT_WITHIN(1, 4000, force[1]);

    SetEffectReportData east{};
    east.effectBlockIndex = index;
    east.effectType = ET_CONSTANT;
    east.duration = 1000;
    east.gain = USB_MAX_EFFECT_GAIN;
    east.triggerButton = USB_NO_TRIGGER_BUTTON;
    east.axisEnable = DIRECTION_ENABLE;
    east.directions[0] = 9000;
    f.handler.set_effect(&east);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, -4000, force[0]);
    TEST_ASSERT_INT_WITHIN(1, 0, force[1]);
}

static void test_ramp_restarts_each_loop()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_RAMP, 100);
    ramp(f, index, 0, 10000);
    f.start(index, 3);
    f.at(50);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 5000, force[1]);
    f.at(100);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 0, force[1]);
    f.at(250);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 5000, force[1]);
    f.at(300);
    f.calculate(force);
    TEST_ASSERT_EQUAL_INT32(0, force[1]);
}

static void test_sine_quadrants()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_SINE);
    periodic(f, index, 8000, 0, 0, 100);
    f.start(index);
    const uint32_t times[] = {0, 25, 50, 75};
    const int32_t expected[] = {0, 8000, 0, -8000};
    for (unsigned i = 0; i < 4; ++i)
    {
        f.at(times[i]);
        f.calculate(force);
        TEST_ASSERT_INT_WITHIN(2, expected[i], force[1]);
    }
}

static void test_envelope_preserves_periodic_offset()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_SQUARE, 200);
    periodic(f, index, 6000, 2000, 0, 100);
    SetEnvelopeReportData envelope{index, 0, 0, 100, 0};
    f.handler.set_envelope(&envelope);
    f.start(index);
    f.at(0);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 2000, force[1]);
    f.at(50);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, -1000, force[1]);
}

static void test_infinite_loop_remains_active()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_CONSTANT, 100);
    constant(f, index, 3000);
    f.start(index, 0xFF);
    f.at(100000);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 3000, force[1]);
    TEST_ASSERT_TRUE(f.handler.is_effect_playing(index, 0, fakeMillis));
}

static void test_pause_freezes_start_delay()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_CONSTANT, 1000, 0, 500);
    constant(f, index, 3000);
    f.start(index);
    f.at(200);
    DeviceControlReportData pause{5};
    f.handler.set_device_control(&pause);
    f.at(1200);
    DeviceControlReportData resume{6};
    f.handler.set_device_control(&resume);
    f.at(1499);
    f.calculate(force);
    TEST_ASSERT_EQUAL_INT32(0, force[1]);
    f.at(1500);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 3000, force[1]);
}

static void test_reset_and_pool_capabilities()
{
    Fixture f;
    uint8_t index = f.create(ET_CONSTANT);
    constant(f, index, 3000);
    f.start(index);
    DeviceControlReportData reset{4};
    f.handler.set_device_control(&reset);
    const PIDStateReportData* state = f.handler.get_pid_state_report_data();
    TEST_ASSERT_BITS_HIGH(0x02, state->status);
    TEST_ASSERT_EQUAL_UINT8(0, state->effectBlockIndex);
    TEST_ASSERT_EQUAL(FFBReportHandler::DEVICE_STATE_ACTIVE, f.handler.deviceState);
    const PoolReportData* pool = f.handler.get_pool_report_data();
    TEST_ASSERT_BITS_HIGH(0x01, pool->managedPool);
    TEST_ASSERT_BITS_LOW(0x02, pool->managedPool);
    TEST_ASSERT_EQUAL_UINT16(pool->ramPoolSize,
                             f.handler.get_block_load_report_data()->ramPoolAvailable);
}

static void test_combined_force_is_clamped()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t a = f.create(ET_CONSTANT);
    uint8_t b = f.create(ET_CONSTANT);
    constant(f, a, 8000);
    constant(f, b, 8000);
    f.start(a);
    f.start(b);
    f.calculate(force);
    TEST_ASSERT_EQUAL_INT32(USB_MAX_MAGNITUDE, force[1]);
}

static void test_gain_scales_constant_force()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_CONSTANT, 1000, 0, 0, 128);
    constant(f, index, 4000);
    f.start(index);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(2, 2008, force[1]);
}

static void test_sample_period_quantizes_waveform()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_SINE, 1000, 0, 0, USB_MAX_EFFECT_GAIN, 20);
    periodic(f, index, 8000, 0, 0, 100);
    f.start(index);
    f.at(10);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 0, force[1]);
    f.at(20);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(3, 7608, force[1]);
}

static void test_spring_position_condition()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    f.input.set_tf_position(0.f);
    uint8_t index = f.create(ET_SPRING, 1000, 0, 0, USB_MAX_EFFECT_GAIN, 0, 0x02);
    condition(f, index, 1, 0, 10000, 10000);
    set_input(f, 0, 16384);
    f.start(index);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(3, -5000, force[1]);
    TEST_ASSERT_EQUAL_INT32(0, force[0]);
}

static void test_damper_uses_speed_metric()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    f.input.set_tf_position(0.f);
    f.input.set_tf_speed(0.f);
    uint8_t index = f.create(ET_DAMPER, 1000, 0, 0, USB_MAX_EFFECT_GAIN, 0, 0x02);
    condition(f, index, 1, 0, 10000, 10000);
    set_input(f, 0, 0, 100000);
    set_input(f, 0, 100, 100000);
    f.start(index);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(4, -305, force[1]);
}

static void test_inertia_uses_acceleration_metric()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    f.input.set_tf_position(0.f);
    f.input.set_tf_speed(0.f);
    uint8_t index = f.create(ET_INERTIA, 1000, 0, 0, USB_MAX_EFFECT_GAIN, 0, 0x02);
    condition(f, index, 1, 0, 10000, 10000);
    set_input(f, 0, 0, 100000);
    set_input(f, 0, 100, 100000);
    set_input(f, 0, 103, 100000);
    f.start(index);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(20, 2960, force[1]);
}

static void test_friction_is_sign_based()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    f.input.set_tf_position(0.f);
    f.input.set_tf_speed(0.f);
    uint8_t index = f.create(ET_FRICTION, 1000, 0, 0, USB_MAX_EFFECT_GAIN, 0, 0x02);
    condition(f, index, 1, 0, 10000, 10000);
    set_input(f, 0, 0, 100000);
    set_input(f, 0, 100, 100000);
    f.start(index);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, -10000, force[1]);
}

static void test_fade_envelope_reduces_constant_force()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_CONSTANT, 200);
    constant(f, index, 6000);
    SetEnvelopeReportData envelope{index, 0, 0, 0, 100};
    f.handler.set_envelope(&envelope);
    f.start(index);
    f.at(150);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 3000, force[1]);
}

static void test_triangle_waveform_quadrants()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_TRIANGLE);
    periodic(f, index, 8000, 0, 0, 100);
    f.start(index);
    const uint32_t times[] = {0, 25, 50, 75};
    const int32_t expected[] = {0, 8000, 0, -8000};
    for (unsigned i = 0; i < 4; ++i)
    {
        f.at(times[i]);
        f.calculate(force);
        TEST_ASSERT_INT_WITHIN(2, expected[i], force[1]);
    }
}

static void test_sawtooth_waveforms_boundaries()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t down = f.create(ET_SAWTOOTH_DOWN);
    uint8_t up = f.create(ET_SAWTOOTH_UP);
    periodic(f, down, 8000, 0, 0, 100);
    periodic(f, up, 8000, 0, 0, 100);
    f.start(down);
    f.start(up);
    f.at(0);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(2, 0, force[1]);
    f.at(50);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(2, 0, force[1]);
    EffectOperationReportData stopUp{up, 3, 1};
    f.handler.set_effect_operation(&stopUp);
    f.at(100);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(2, 8000, force[1]);
}

static void test_sine_phase_quarter_cycle()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_SINE);
    periodic(f, index, 8000, 0, 9000, 100);
    f.start(index);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(2, 8000, force[1]);
}

static void test_condition_deadband_and_saturation()
{
    Fixture deadband;
    int32_t force[NUM_AXIS];
    deadband.input.set_tf_position(0.f);
    uint8_t deadIndex = deadband.create(ET_SPRING, 1000, 0, 0, USB_MAX_EFFECT_GAIN, 0, 0x02);
    condition(deadband, deadIndex, 1, 0, 10000, 10000, 10000, 10000, 20000);
    set_input(deadband, 0, 16384);
    deadband.start(deadIndex);
    deadband.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 0, force[1]);

    Fixture saturation;
    saturation.input.set_tf_position(0.f);
    uint8_t satIndex = saturation.create(ET_SPRING, 1000, 0, 0, USB_MAX_EFFECT_GAIN, 0, 0x02);
    condition(saturation, satIndex, 1, 0, 10000, 10000, 2000, 2000);
    set_input(saturation, 0, 32767);
    saturation.start(satIndex);
    saturation.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, -2000, force[1]);
}

static void test_effect_duration_boundary()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_CONSTANT, 100);
    constant(f, index, 3000);
    f.start(index);
    f.at(99);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 3000, force[1]);
    f.at(100);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 0, force[1]);
    TEST_ASSERT_FALSE(f.handler.is_effect_playing(index, 0, fakeMillis));
}

static void test_button_trigger_lifecycle()
{
    Fixture f;
    int32_t force[NUM_AXIS];
    uint8_t index = f.create(ET_CONSTANT, 100, 0, 0, USB_MAX_EFFECT_GAIN, 0, DIRECTION_ENABLE, 1);
    constant(f, index, 3000);
    f.start(index);
    f.input.update_buttons(0);
    f.calculate(force);
    TEST_ASSERT_EQUAL_INT32(0, force[1]);
    f.input.update_buttons(0x01);
    f.calculate(force);
    TEST_ASSERT_INT_WITHIN(1, 3000, force[1]);
    f.input.update_buttons(0);
    f.calculate(force);
    TEST_ASSERT_EQUAL_INT32(3000, force[1]);
    f.at(100);
    f.calculate(force);
    TEST_ASSERT_EQUAL_INT32(0, force[1]);
}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_constant_force_and_direction);
    RUN_TEST(test_ramp_restarts_each_loop);
    RUN_TEST(test_sine_quadrants);
    RUN_TEST(test_envelope_preserves_periodic_offset);
    RUN_TEST(test_infinite_loop_remains_active);
    RUN_TEST(test_pause_freezes_start_delay);
    RUN_TEST(test_reset_and_pool_capabilities);
    RUN_TEST(test_combined_force_is_clamped);
    RUN_TEST(test_gain_scales_constant_force);
    RUN_TEST(test_sample_period_quantizes_waveform);
    RUN_TEST(test_spring_position_condition);
    RUN_TEST(test_damper_uses_speed_metric);
    RUN_TEST(test_inertia_uses_acceleration_metric);
    RUN_TEST(test_friction_is_sign_based);
    RUN_TEST(test_fade_envelope_reduces_constant_force);
    RUN_TEST(test_triangle_waveform_quadrants);
    RUN_TEST(test_sawtooth_waveforms_boundaries);
    RUN_TEST(test_sine_phase_quarter_cycle);
    RUN_TEST(test_condition_deadband_and_saturation);
    RUN_TEST(test_effect_duration_boundary);
    RUN_TEST(test_button_trigger_lifecycle);
    return UNITY_END();
}
