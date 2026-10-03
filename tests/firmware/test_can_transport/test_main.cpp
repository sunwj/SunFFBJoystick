/**
 * Native tests for motor CAN codecs and TWAI adaptation using in-memory frames and a fake driver.
 * Cover endianness, axis count, sequence wrap/duplicates, backpressure, alerts and bus-off recovery.
 * Simulated acceptance does not validate arbitration or physical bus timing.
 */

#include <unity.h>
#include <deque>
#include <cstring>
#include "motor_protocol/can_link.h"
#include <EmbeddedComm/Esp32TwaiHal.h>

using namespace SunFFB;
extern "C" uint32_t _millis()
{
    return 0;
}

extern "C" uint32_t _micros()
{
    return 0;
}

extern "C" void _debug_printf(const char*, ...)
{
}

void setUp()
{
    TwaiStub::reset();
}

void tearDown()
{
}

struct Hal
{
    // In-memory CAN loopback; busy rejects submission so tests can check sequence ownership.
    std::deque<CANFrame> frames;
    bool busy = false;
    bool send(const CANFrame& f)
    {
        if (busy)
            return false;

        frames.push_back(f);
        return true;
    }

    bool receive(CANFrame& f)
    {
        if (frames.empty())
            return false;

        f = frames.front();
        frames.pop_front();
        return true;
    }
};

void test_force_vector_wire_and_bounds()
{
    int32_t forces[NUM_AXIS]{};
    forces[0] = -10000;
    CANFrame frame;
    TEST_ASSERT_TRUE(encode_can_force(frame, forces, 255));
    TEST_ASSERT_EQUAL_UINT8(2 + 2 * NUM_AXIS, frame.length);
    TEST_ASSERT_EQUAL_UINT8(0x10 | NUM_AXIS, frame.data[0]);
    TEST_ASSERT_EQUAL_HEX8(0xF0, frame.data[2]);
    TEST_ASSERT_EQUAL_HEX8(0xD8, frame.data[3]);
    ForcePayload decoded{};
    TEST_ASSERT_TRUE(decode_can_force(frame, decoded));
    TEST_ASSERT_EQUAL_INT32(-10000, decoded.force[0]);
    forces[0] = 10001;
    TEST_ASSERT_FALSE(encode_can_force(frame, forces, 0));
    can_store_u16(frame.data + 2, 32767);
    decoded.force[0] = 321;
    TEST_ASSERT_FALSE(decode_can_force(frame, decoded));
    TEST_ASSERT_EQUAL_INT32(321, decoded.force[0]);
}

void test_position_vector_little_endian_and_validation()
{
    uint16_t values[NUM_AXIS]{};
    values[0] = 65535;
    CANFrame good;
    TEST_ASSERT_TRUE(encode_can_position(good, values, 0));
    PositionPayload out{};
    TEST_ASSERT_TRUE(decode_can_position(good, out));
    TEST_ASSERT_EQUAL_UINT16(65535, out.position[0]);
    out.position[0] = 123;
    auto bad = good;
    bad.length = 9;
    TEST_ASSERT_FALSE(decode_can_position(bad, out));
    bad = good;
    bad.extended = true;
    TEST_ASSERT_FALSE(decode_can_position(bad, out));
    bad = good;
    bad.remote = true;
    TEST_ASSERT_FALSE(decode_can_position(bad, out));
    bad = good;
    bad.id = 0x180;
    TEST_ASSERT_FALSE(decode_can_position(bad, out));
    bad = good;
    bad.data[0] ^= 1;
    TEST_ASSERT_FALSE(decode_can_position(bad, out));
    bad = good;
    bad.id = 0x800;
    TEST_ASSERT_FALSE(decode_can_position(bad, out, 0x800));
    TEST_ASSERT_EQUAL_UINT16(123, out.position[0]);
}

void test_position_duplicates_wrap_and_peer_reset()
{
    Hal hal;
    FFBCANLink<Hal> link(hal);
    uint16_t pos[NUM_AXIS]{};
    pos[0] = 42;
    CANFrame frame;
    encode_can_position(frame, pos, 255);
    hal.frames.push_back(frame);
    PositionPayload out{};
    TEST_ASSERT_EQUAL_UINT8(uint8_t(CANReceiveResult::Position),
                            uint8_t(link.pollPosition(out, 0xFFFFFF00u)));
    out.position[0] = 100;
    hal.frames.push_back(frame);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(CANReceiveResult::Ignored),
                            uint8_t(link.pollPosition(out, 10)));
    TEST_ASSERT_EQUAL_UINT16(100, out.position[0]);
    frame.data[1] = 0;
    hal.frames.push_back(frame);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(CANReceiveResult::Position),
                            uint8_t(link.pollPosition(out, 20)));
    hal.frames.push_back(frame);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(CANReceiveResult::Position),
                            uint8_t(link.pollPosition(out, 100020)));
    link.resetReceiver();
    hal.frames.push_back(frame);
    TEST_ASSERT_EQUAL_UINT8(uint8_t(CANReceiveResult::Position),
                            uint8_t(link.pollPosition(out, 100021)));
    TEST_ASSERT_EQUAL_UINT32(1, link.getDuplicates());
}

void test_backpressure_does_not_advance_sequence()
{
    Hal hal;
    FFBCANLink<Hal> link(hal);
    int32_t forces[NUM_AXIS]{};
    hal.busy = true;
    TEST_ASSERT_FALSE(link.sendForce(forces));
    hal.busy = false;
    TEST_ASSERT_TRUE(link.sendForce(forces));
    TEST_ASSERT_EQUAL_UINT8(0, hal.frames.back().data[1]);
    TEST_ASSERT_TRUE(link.sendForce(forces));
    TEST_ASSERT_EQUAL_UINT8(1, hal.frames.back().data[1]);
}

void test_custom_ids_heartbeat_and_unrelated_frames()
{
    Hal hal;
    FFBCANLink<Hal> link(hal, {0x300, 0x301, 0x302});
    TEST_ASSERT_TRUE(link.sendHeartbeat(5));
    TEST_ASSERT_EQUAL_UINT32(0x302, hal.frames.front().id);
    TEST_ASSERT_EQUAL_UINT8(3, hal.frames.front().length);
    PositionPayload out{};
    TEST_ASSERT_EQUAL_UINT8(uint8_t(CANReceiveResult::Ignored), uint8_t(link.pollPosition(out, 0)));
    uint16_t pos[NUM_AXIS]{};
    pos[0] = 555;
    TEST_ASSERT_TRUE(link.sendPosition(pos));
    TEST_ASSERT_EQUAL_UINT8(uint8_t(CANReceiveResult::Position),
                            uint8_t(link.pollPosition(out, 1)));
    TEST_ASSERT_EQUAL_UINT16(555, out.position[0]);
    FFBCANLink<Hal> invalid(hal, {0x300, 0x300, 0x302});
    TEST_ASSERT_FALSE(invalid.sendHeartbeat());
}

void test_twiai_config_and_nonblocking_transmit()
{
    EmbeddedComm::Esp32TwaiHal hal;
    TEST_ASSERT_TRUE(hal.begin(4, 5, 500000, 0x181));
    TEST_ASSERT_EQUAL_UINT32(0, TwaiStub::general.tx_queue_len);
    TEST_ASSERT_EQUAL_UINT32(16, TwaiStub::general.rx_queue_len);
    TEST_ASSERT_EQUAL_UINT32(0x181u << 21, TwaiStub::filter.acceptance_code);
    TEST_ASSERT_EQUAL_UINT32(500000, TwaiStub::timing.rate);
    CANFrame frame;
    frame.id = 0x201;
    frame.length = 8;
    TEST_ASSERT_TRUE(hal.send(frame));
    TEST_ASSERT_FALSE(TwaiStub::lastTx.ss);
    TwaiStub::busy = true;
    TEST_ASSERT_FALSE(hal.send(frame));
    TEST_ASSERT_EQUAL_UINT32(1, hal.stats().accepted);
    TEST_ASSERT_EQUAL_UINT32(1, hal.stats().rejected);
}

void test_optional_single_shot_and_invalid_dlc_receive()
{
    EmbeddedComm::Esp32TwaiHal hal;
    TEST_ASSERT_TRUE(hal.begin(4, 5, 500000, 0x181, true));
    CANFrame frame;
    frame.id = 0x201;
    TEST_ASSERT_TRUE(hal.send(frame));
    TEST_ASSERT_TRUE(TwaiStub::lastTx.ss);
    twai_message_t input{};
    input.identifier = 0x181;
    input.data_length_code = 9;
    TwaiStub::rx.push_back(input);
    TEST_ASSERT_TRUE(hal.receive(frame));
    PositionPayload out{};
    TEST_ASSERT_FALSE(decode_can_position(frame, out));
    TwaiStub::alerts = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_RX_QUEUE_FULL;
    hal.service();
    TEST_ASSERT_EQUAL_UINT32(1, hal.stats().successAlerts);
    TEST_ASSERT_EQUAL_UINT32(1, hal.stats().overflowAlerts);
}

void test_bus_off_recovery_and_restart_failure()
{
    EmbeddedComm::Esp32TwaiHal hal;
    TEST_ASSERT_TRUE(hal.begin(4, 5, 500000, 0x181));
    TwaiStub::state = TWAI_STATE_BUS_OFF;
    TwaiStub::alerts = TWAI_ALERT_BUS_OFF | TWAI_ALERT_TX_FAILED;
    TEST_ASSERT_FALSE(hal.service());
    TEST_ASSERT_FALSE(hal.running());
    TEST_ASSERT_EQUAL_INT(1, TwaiStub::recoveries);
    CANFrame frame;
    TEST_ASSERT_FALSE(hal.send(frame));
    TEST_ASSERT_FALSE(hal.service());
    TEST_ASSERT_EQUAL_INT(1, TwaiStub::recoveries);
    TwaiStub::state = TWAI_STATE_STOPPED;
    TwaiStub::alerts = TWAI_ALERT_BUS_RECOVERED;
    TwaiStub::startFails = true;
    TEST_ASSERT_FALSE(hal.service());
    TEST_ASSERT_FALSE(hal.running());
    TwaiStub::startFails = false;
    TEST_ASSERT_TRUE(hal.service());
    TEST_ASSERT_TRUE(hal.running());
    TEST_ASSERT_EQUAL_UINT32(1, hal.stats().recoveries);
}

void test_start_failure_cleanup_and_bad_configuration()
{
    EmbeddedComm::Esp32TwaiHal hal;
    TEST_ASSERT_FALSE(hal.begin(4, 4, 500000, 0x181));
    TEST_ASSERT_FALSE(hal.begin(4, 5, 333333, 0x181));
    TwaiStub::startFails = true;
    TEST_ASSERT_FALSE(hal.begin(4, 5, 500000, 0x181));
    TEST_ASSERT_EQUAL_INT(1, TwaiStub::uninstalls);
    TwaiStub::startFails = false;
    TEST_ASSERT_TRUE(hal.begin(4, 5, 500000, 0x181));
    TEST_ASSERT_FALSE(hal.begin(4, 5, 500000, 0x181));
}

void test_generic_twiai_accept_all_filter()
{
    EmbeddedComm::Esp32TwaiHal hal;
    TEST_ASSERT_TRUE(hal.begin(4, 5, 500000));
    TEST_ASSERT_EQUAL_HEX32(0, TwaiStub::filter.acceptance_code);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, TwaiStub::filter.acceptance_mask);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_generic_twiai_accept_all_filter);
    RUN_TEST(test_force_vector_wire_and_bounds);
    RUN_TEST(test_position_vector_little_endian_and_validation);
    RUN_TEST(test_position_duplicates_wrap_and_peer_reset);
    RUN_TEST(test_backpressure_does_not_advance_sequence);
    RUN_TEST(test_custom_ids_heartbeat_and_unrelated_frames);
    RUN_TEST(test_twiai_config_and_nonblocking_transmit);
    RUN_TEST(test_optional_single_shot_and_invalid_dlc_receive);
    RUN_TEST(test_bus_off_recovery_and_restart_failure);
    RUN_TEST(test_start_failure_cleanup_and_bad_configuration);
    return UNITY_END();
}
