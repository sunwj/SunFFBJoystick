#include "realtime_timing.h"
#include <unity.h>
#include <cstring>
#include <vector>
#include "ffb_force_calculator.h"
#include "ffb_report_dispatch.h"
#include "communication/serial_link.h"

using namespace SunFFB;
static uint32_t nowMs, nowUs;
extern "C" uint32_t _millis(void)
{
    return nowMs;
}

extern "C" uint32_t _micros(void)
{
    return nowUs;
}

extern "C" void _debug_printf(const char*, ...)
{
}

void setUp()
{
    nowMs = nowUs = 0;
}

void tearDown()
{
}

struct Fixture
{
    FFBReportHandler handler;
    FFBDeviceInput input;
    FFBForceCalculator calculator;
    Fixture()
    {
        input.reset();
        input.set_tf_position(0);
    }

    uint8_t effect(uint8_t type = ET_CONSTANT, uint16_t duration = 1000, uint16_t delay = 0,
                   uint8_t trigger = USB_NO_TRIGGER_BUTTON)
    {
        CreateNewEffectReportData create{type};
        handler.create_new_effect(&create);
        uint8_t id = handler.get_block_load_report_data()->effectBlockIndex;
        SetEffectReportData data{};
        data.effectBlockIndex = id;
        data.effectType = type;
        data.duration = duration;
        data.startDelay = delay;
        data.triggerButton = trigger;
        data.gain = 255;
        data.axisEnable = X_AXIS_ENABLE;
        handler.set_effect(&data);

        // Use an X-only vector to keep timing/storage tests axis-count independent.
        auto& block = handler.get_all_effect_blocks()[id - 1];
        memset(block.directionUnitVector, 0, sizeof(block.directionUnitVector));
        block.directionUnitVector[0] = 1;
        SetConstantForceReportData constant{id, 6000};
        if (type == ET_CONSTANT)
            handler.set_constant_force(&constant);
        return id;
    }

    void start(uint8_t id)
    {
        EffectOperationReportData op{id, 1, 1};
        handler.set_effect_operation(&op);
    }

    void control(uint8_t command)
    {
        DeviceControlReportData data{command};
        handler.set_device_control(&data);
    }

    int32_t force()
    {
        int32_t values[NUM_AXIS]{};
        calculator.force_calculator(handler, input, values);
        return values[0];
    }
};

void test_invalid_reports_leave_state_unchanged()
{
    Fixture f;
    const uint16_t pool = f.handler.get_block_load_report_data()->ramPoolAvailable;
    uint8_t bytes[sizeof(SetEffectReportData) + 1]{};
    TEST_ASSERT_FALSE(dispatch_output_report(f.handler, REPORT_ID_SET_EFFECT_REPORT, 2, bytes,
                                             sizeof(SetEffectReportData) - 1));
    TEST_ASSERT_FALSE(
        dispatch_output_report(f.handler, REPORT_ID_SET_EFFECT_REPORT, 2, bytes, sizeof(bytes)));
    TEST_ASSERT_FALSE(dispatch_output_report(f.handler, REPORT_ID_DEVICE_GAIN_REPORT, 3, bytes, 1));
    TEST_ASSERT_FALSE(
        dispatch_output_report(f.handler, REPORT_ID_CREATE_NEW_EFFECT_REPORT, 2, bytes, 1));
    TEST_ASSERT_FALSE(dispatch_output_report(f.handler, 99, 2, bytes, 1));
    TEST_ASSERT_FALSE(
        dispatch_output_report(f.handler, REPORT_ID_DEVICE_GAIN_REPORT, 2, nullptr, 1));
    TEST_ASSERT_EQUAL_UINT8(255, f.handler.deviceGain);
    TEST_ASSERT_EQUAL_UINT16(pool, f.handler.get_block_load_report_data()->ramPoolAvailable);
    uint8_t gain = 64;
    TEST_ASSERT_TRUE(dispatch_output_report(f.handler, REPORT_ID_DEVICE_GAIN_REPORT, 2, &gain, 1));
    TEST_ASSERT_EQUAL_UINT8(64, f.handler.deviceGain);
}

void test_envelope_does_not_corrupt_adjacent_effect()
{
    Fixture f;
    uint8_t first = f.effect(), second = f.effect();
    auto before = f.handler.get_all_effect_blocks()[second - 1];
    SetEnvelopeReportData envelope{first, 0, 0, 100, 100};
    f.handler.set_envelope(&envelope);
    TEST_ASSERT_EQUAL_MEMORY(&before, &f.handler.get_all_effect_blocks()[second - 1],
                             sizeof(before));
    f.start(first);
    nowMs = 50;
    TEST_ASSERT_EQUAL_INT32(3000, f.force());
}

void test_disabled_stays_disabled_after_pause_resume()
{
    Fixture f;
    uint8_t id = f.effect();
    f.start(id);
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
    f.control(2);
    f.control(6);
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 10;
    f.control(5);
    nowMs = 100;
    f.control(6);
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    TEST_ASSERT_BITS_LOW(2, f.handler.get_pid_state_report_data()->status);
    f.control(1);
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
}

void test_cold_boot_pause_preserves_implicit_enable()
{
    Fixture f;
    f.control(5);
    f.control(6);
    TEST_ASSERT_EQUAL(FFBReportHandler::DEVICE_STATE_INIT, f.handler.deviceState);
    f.control(5);
    uint8_t id = f.effect();
    f.start(id);
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    f.control(6);
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
}

void test_pause_resume_commands_are_idempotent()
{
    Fixture f;
    uint8_t id = f.effect();
    f.start(id);
    nowMs = 100;
    f.control(6);
    TEST_ASSERT_EQUAL_UINT32(0, f.handler.get_all_effect_blocks()[id - 1].startTime);
    f.control(5);
    nowMs = 200;
    f.control(5);
    f.control(1);
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 300;
    f.control(6);
    TEST_ASSERT_EQUAL_UINT32(200, f.handler.get_all_effect_blocks()[id - 1].startTime);
    nowMs = 400;
    f.control(6);
    TEST_ASSERT_EQUAL_UINT32(200, f.handler.get_all_effect_blocks()[id - 1].startTime);
}

void test_effect_started_during_pause_preserves_full_delay()
{
    Fixture f;
    f.control(1);
    nowMs = 100;
    f.control(5);
    nowMs = 200;
    uint8_t id = f.effect(ET_CONSTANT, 1000, 50);
    f.start(id);
    nowMs = 300;
    f.control(6);
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 349;
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 350;
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
}

void test_envelope_does_not_change_condition_blocks()
{
    Fixture f;
    uint8_t id = f.effect(ET_SPRING);

    for (uint8_t axis = 0; axis < NUM_AXIS; ++axis)
    {
        SetConditionReportData condition{id, axis, 0, 10000, 10000, 2000, 3000, 0};
        f.handler.set_condition(&condition);
    }

    auto before = f.handler.get_all_effect_blocks()[id - 1];
    SetEnvelopeReportData envelope{id, 0, 0, 100, 100};
    f.handler.set_envelope(&envelope);
    TEST_ASSERT_EQUAL_MEMORY(&before, &f.handler.get_all_effect_blocks()[id - 1], sizeof(before));
}

void test_trigger_waits_for_initial_and_repeat_delay()
{
    Fixture f;
    uint8_t id = f.effect(ET_CONSTANT, 100, 50, 1);
    f.start(id);
    f.input.update_buttons(1);
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 49;
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 50;
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
    nowMs = 150;
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 199;
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 200;
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
}

void test_trigger_infinite_duration_and_invalid_button()
{
    Fixture f;
    uint8_t id = f.effect(ET_CONSTANT, USB_DURATION_INFINITE, 0, 1);
    f.start(id);
    f.input.update_buttons(1);
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
    nowMs = 100000;
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
    f.handler.get_all_effect_blocks()[id - 1].effectData.triggerButton = 33;
    TEST_ASSERT_EQUAL_INT32(0, f.force());
}

void test_delayed_trigger_across_clock_wrap()
{
    Fixture f;
    nowMs = 0xFFFFFFE0U;
    uint8_t id = f.effect(ET_CONSTANT, 100, 50, 1);
    f.start(id);
    f.input.update_buttons(1);
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 17;
    TEST_ASSERT_EQUAL_INT32(0, f.force());
    nowMs = 18;
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
}

void test_fade_longer_than_duration()
{
    Fixture f;
    uint8_t id = f.effect(ET_CONSTANT, 100);
    SetEnvelopeReportData envelope{id, 0, 0, 0, 200};
    f.handler.set_envelope(&envelope);
    f.start(id);
    nowMs = 50;
    TEST_ASSERT_EQUAL_INT32(4500, f.force());
}

void test_infinite_duration_ignores_fade()
{
    Fixture f;
    uint8_t id = f.effect(ET_CONSTANT, USB_DURATION_INFINITE);
    SetEnvelopeReportData envelope{id, 0, 0, 0, 200};
    f.handler.set_envelope(&envelope);
    f.start(id);
    nowMs = 100000;
    TEST_ASSERT_EQUAL_INT32(6000, f.force());
}

void test_negative_coefficients_respect_asymmetric_saturation()
{
    Fixture f;
    uint8_t id = f.effect(ET_SPRING);
    SetConditionReportData condition{id, 0, 0, -10000, -10000, 2000, 3000, 0};
    f.handler.set_condition(&condition);
    f.start(id);
    int16_t axes[NUM_AXIS]{};
    axes[0] = 32767;
    nowUs = 10000;
    f.input.update_axis(axes);
    TEST_ASSERT_EQUAL_INT32(2000, f.force());
    axes[0] = -32767;
    nowUs = 20000;
    f.input.update_axis(axes);
    TEST_ASSERT_EQUAL_INT32(-3000, f.force());
}

struct MockSerial
{
    std::vector<uint8_t> bytes;
    size_t readIndex = 0;
    int available()
    {
        return bytes.size() - readIndex;
    }

    uint8_t read()
    {
        return bytes[readIndex++];
    }

    size_t write(const uint8_t* data, uint16_t length)
    {
        bytes.insert(bytes.end(), data, data + length);
        return length;
    }

    void heartbeat()
    {
        uint8_t data[] = {0xAA, SERIAL_MSG_HEARTBEAT, 0,
                          calc_frame_crc8(SERIAL_MSG_HEARTBEAT, nullptr, 0)};
        write(data, 4);
    }
};

void test_serial_returns_each_frame_in_order()
{
    MockSerial serial;
    FFBSerialLink<MockSerial> link(serial);
    uint16_t first[NUM_AXIS]{}, second[NUM_AXIS]{};
    first[0] = 123;
    second[0] = 456;
    link.sendPosition(first);
    serial.heartbeat();
    link.sendPosition(second);
    PositionPayload position{};
    TEST_ASSERT_TRUE(link.receivePosition(position));
    TEST_ASSERT_EQUAL_UINT16(123, position.position[0]);
    uint8_t payload[64];
    TEST_ASSERT_EQUAL_UINT8(SERIAL_MSG_HEARTBEAT, link.receive(payload));
    TEST_ASSERT_TRUE(link.receivePosition(position));
    TEST_ASSERT_EQUAL_UINT16(456, position.position[0]);
    TEST_ASSERT_EQUAL_UINT8(0, link.receive(payload));
}

void test_serial_partial_and_bad_crc_recovery()
{
    MockSerial serial;
    FFBSerialLink<MockSerial> link(serial);
    uint16_t values[NUM_AXIS]{};
    values[0] = 123;
    link.sendPosition(values);
    auto frame = serial.bytes;
    serial.bytes.resize(3);
    PositionPayload position{};
    TEST_ASSERT_FALSE(link.receivePosition(position));
    serial.bytes.insert(serial.bytes.end(), frame.begin() + 3, frame.end());
    TEST_ASSERT_TRUE(link.receivePosition(position));
    serial.bytes.insert(serial.bytes.end(), frame.begin(), frame.end());
    serial.bytes.back() ^= 1;
    serial.bytes.insert(serial.bytes.end(), frame.begin(), frame.end());
    TEST_ASSERT_TRUE(link.receivePosition(position));
    TEST_ASSERT_EQUAL_UINT32(1, link.getCrcErrors());
}

void test_serial_noise_has_bounded_work_and_recovers()
{
    MockSerial serial;
    FFBSerialLink<MockSerial> link(serial);
    serial.bytes.resize(200, 0); // No sync bytes: consume only one budget per call.
    uint16_t values[NUM_AXIS]{};
    values[0] = 789;
    link.sendPosition(values);
    PositionPayload position{};
    TEST_ASSERT_FALSE(link.receivePosition(position));
    TEST_ASSERT_EQUAL_UINT32(68, serial.readIndex);
    TEST_ASSERT_FALSE(link.receivePosition(position));
    TEST_ASSERT_EQUAL_UINT32(136, serial.readIndex);

    while (!link.receivePosition(position))
    {
        TEST_ASSERT_LESS_THAN_UINT32(serial.bytes.size(), serial.readIndex);
    }

    TEST_ASSERT_EQUAL_UINT16(789, position.position[0]);
}

void test_report_schedule_retries_and_preserves_phase()
{
    ReportSchedule schedule(2000);
    TEST_ASSERT_FALSE(schedule.due(1999));
    TEST_ASSERT_TRUE(schedule.due(2000));

    // Failed submit: do not call sent(), next opportunity remains due.
    TEST_ASSERT_TRUE(schedule.due(3000));
    schedule.sent(3100);
    TEST_ASSERT_FALSE(schedule.due(3999));
    TEST_ASSERT_TRUE(schedule.due(4000));
    schedule.sent(9100); // skip missed deadlines without burst replay
    TEST_ASSERT_FALSE(schedule.due(9999));
    TEST_ASSERT_TRUE(schedule.due(10000));
}

void test_timing_wrap_and_stalled_stream()
{
    ReportSchedule schedule(2000, 0xfffffff0U);
    TEST_ASSERT_FALSE(schedule.due(0xfffffff0U + 1999U));
    TEST_ASSERT_TRUE(schedule.due(0xfffffff0U + 2000U));
    TimingStream stream(2000);
    stream.record(0xfffffff0U, 50);
    stream.record(0xfffffff0U + 2000U, 30);
    stream.record(0xfffffff0U + 5000U, 80);
    stream.failed();
    const auto stat = stream.snapshot();
    TEST_ASSERT_EQUAL_UINT32(3, stat.count);
    TEST_ASSERT_EQUAL_UINT32(3000, stat.maxGapUs);
    TEST_ASSERT_EQUAL_UINT32(1, stat.overBudget);
    TEST_ASSERT_EQUAL_UINT32(80, stat.maxWorkUs);
    TEST_ASSERT_EQUAL_UINT32(1, stat.failures);
}

struct PartialSerial : MockSerial
{
    size_t write(const uint8_t*, uint16_t length)
    {
        return length - 1;
    }
};

void test_force_transmit_reports_short_write()
{
    int32_t forces[NUM_AXIS]{};
    MockSerial serial;
    FFBSerialLink<MockSerial> good(serial);
    TEST_ASSERT_TRUE(good.sendForce(forces));
    PartialSerial partial;
    FFBSerialLink<PartialSerial> bad(partial);
    TEST_ASSERT_FALSE(bad.sendForce(forces));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_force_transmit_reports_short_write);
    RUN_TEST(test_report_schedule_retries_and_preserves_phase);
    RUN_TEST(test_timing_wrap_and_stalled_stream);
    RUN_TEST(test_invalid_reports_leave_state_unchanged);
    RUN_TEST(test_envelope_does_not_corrupt_adjacent_effect);
    RUN_TEST(test_disabled_stays_disabled_after_pause_resume);
    RUN_TEST(test_cold_boot_pause_preserves_implicit_enable);
    RUN_TEST(test_pause_resume_commands_are_idempotent);
    RUN_TEST(test_effect_started_during_pause_preserves_full_delay);
    RUN_TEST(test_envelope_does_not_change_condition_blocks);
    RUN_TEST(test_trigger_waits_for_initial_and_repeat_delay);
    RUN_TEST(test_trigger_infinite_duration_and_invalid_button);
    RUN_TEST(test_delayed_trigger_across_clock_wrap);
    RUN_TEST(test_fade_longer_than_duration);
    RUN_TEST(test_infinite_duration_ignores_fade);
    RUN_TEST(test_negative_coefficients_respect_asymmetric_saturation);
    RUN_TEST(test_serial_returns_each_frame_in_order);
    RUN_TEST(test_serial_partial_and_bad_crc_recovery);
    RUN_TEST(test_serial_noise_has_bounded_work_and_recovers);
    return UNITY_END();
}
