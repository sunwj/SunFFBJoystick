#include <unity.h>
#include <vector>
#include <cstring>

#ifdef SERIAL_TRANSPORT_BENCHMARK
#include <chrono>
#include <cstdio>

#endif
#include "communication/serial_link.h"

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
}

void tearDown()
{
}

struct BulkHal
{
    std::vector<uint8_t> bytes;
    size_t cursor = 0, reads = 0, writes = 0;
    bool full = false;
    int available()
    {
        return bytes.size() - cursor;
    }

    size_t readSome(uint8_t* out, size_t size)
    {
        ++reads;
        size_t count = std::min(size, bytes.size() - cursor);
        memcpy(out, bytes.data() + cursor, count);
        cursor += count;
        return count;
    }

    size_t write(const uint8_t* data, size_t size)
    {
        ++writes;
        if (full)
            return 0;

        bytes.insert(bytes.end(), data, data + size);
        return size;
    }
};

void test_legacy_wire_bytes_and_crc_vector()
{
    const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX8(0xA1, calc_crc8(check, 9));
    BulkHal hal;
    FFBSerialLink<BulkHal> link(hal);
    uint8_t data[] = {0xAA, 0xAB, 0};
    TEST_ASSERT_TRUE(link.sendRaw(0x40, data, 3));
    uint8_t expected[] = {0xAA, 0x40, 3, 0xAA, 0xAB, 0, calc_frame_crc8(0x40, data, 3)};
    TEST_ASSERT_EQUAL_MEMORY(expected, hal.bytes.data(), sizeof(expected));
    SerialFrameView frame;
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_MEMORY(data, frame.payload, 3);
    TEST_ASSERT_EQUAL_UINT32(1, hal.reads);
}

void test_fixed_motor_frames_and_heartbeat()
{
    BulkHal hal;
    FFBSerialLink<BulkHal, SerialFraming::Fixed> link(hal);
    uint16_t pos[NUM_AXIS]{};
    pos[0] = 0xABAA;
    TEST_ASSERT_TRUE(link.sendPosition(pos));
    TEST_ASSERT_TRUE(link.sendHeartbeat());
    TEST_ASSERT_EQUAL_HEX8(0xAB, hal.bytes[0]);
    TEST_ASSERT_EQUAL_UINT32(sizeof(PositionPayload) + 3 + 3, hal.bytes.size());
    PositionPayload out{};
    TEST_ASSERT_TRUE(link.receivePosition(out));
    TEST_ASSERT_EQUAL_UINT16(pos[0], out.position[0]);
    SerialFrameView frame;
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_UINT8(SERIAL_MSG_HEARTBEAT, frame.messageId);
    TEST_ASSERT_EQUAL_UINT8(0, frame.length);
    TEST_ASSERT_FALSE(link.receiveFrame(frame));
}

void test_mixed_preserves_batched_frame_order_and_views()
{
    BulkHal hal;
    FFBSerialLink<BulkHal, SerialFraming::Mixed> link(hal);
    uint16_t pos[NUM_AXIS]{};
    for (int n = 0; n < 100; ++n)
    {
        pos[0] = n;
        TEST_ASSERT_TRUE(
            link.sendRaw(SERIAL_MSG_POSITION, (const uint8_t*)pos, sizeof(pos),
                         n % 2 ? SerialFrameFormat::Fixed : SerialFrameFormat::Variable));
    }

    const size_t length = hal.bytes.size();

    for (int n = 0; n < 100; ++n)
    {
        SerialFrameView frame;
        TEST_ASSERT_TRUE(link.receiveFrame(frame));
        uint16_t value;
        memcpy(&value, frame.payload, 2);
        TEST_ASSERT_EQUAL_UINT16(n, value);
        TEST_ASSERT_EQUAL_UINT8(n % 2 ? 1 : 0, uint8_t(frame.format));
    }

    TEST_ASSERT_EQUAL_UINT32((length + 31) / 32, hal.reads);
    TEST_ASSERT_FALSE(link.hasPendingInput());
}

struct CustomLayout
{
    static constexpr uint16_t length(uint8_t id)
    {
        return id == 0x42 ? 64 : 0xFFFF;
    }
};

void test_custom_fixed_layout_and_max_payload()
{
    BulkHal hal;
    FFBSerialLink<BulkHal, SerialFraming::Mixed, 64, CustomLayout> link(hal);
    uint8_t data[64];

    for (int i = 0; i < 64; ++i)
        data[i] = i;
    TEST_ASSERT_TRUE(link.sendRaw(0x42, data, 64, SerialFrameFormat::Fixed));
    SerialFrameView frame;
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_MEMORY(data, frame.payload, 64);
    TEST_ASSERT_FALSE(link.sendRaw(0x42, data, 63, SerialFrameFormat::Fixed));
    TEST_ASSERT_FALSE(link.sendRaw(0x43, data, 64, SerialFrameFormat::Fixed));
}

void test_255_byte_variable_frame_and_empty_id_zero()
{
    BulkHal hal;
    FFBSerialLink<BulkHal, SerialFraming::Variable, 255> link(hal);
    uint8_t data[255];
    memset(data, 0xAA, 255);
    TEST_ASSERT_TRUE(link.sendRaw(255, data, 255));
    TEST_ASSERT_TRUE(link.sendRaw(0, nullptr, 0));
    SerialFrameView frame;
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_UINT8(255, frame.length);
    TEST_ASSERT_EQUAL_MEMORY(data, frame.payload, 255);
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_UINT8(0, frame.messageId);
    TEST_ASSERT_EQUAL_UINT8(0, frame.length);
}

void test_split_frames_and_crc_recovery()
{
    BulkHal hal;
    FFBSerialLink<BulkHal, SerialFraming::Fixed> link(hal);
    uint16_t pos[NUM_AXIS]{};
    pos[0] = 200;
    link.sendPosition(pos);
    auto full = hal.bytes;
    hal.bytes.clear();
    SerialFrameView frame;

    for (size_t i = 0; i < full.size(); ++i)
    {
        hal.bytes.push_back(full[i]);
        TEST_ASSERT_EQUAL(i == full.size() - 1, link.receiveFrame(frame));
    }

    auto bad = full;
    bad.back() ^= 1;
    hal.bytes.insert(hal.bytes.end(), bad.begin(), bad.end());
    hal.bytes.insert(hal.bytes.end(), full.begin(), full.end());
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_UINT32(1, link.getCrcErrors());
}

void test_noise_work_budget_and_invalid_lengths()
{
    BulkHal hal;
    FFBSerialLink<BulkHal, SerialFraming::Mixed> link(hal);
    hal.bytes.resize(200, 0);
    SerialFrameView frame;
    TEST_ASSERT_FALSE(link.receiveFrame(frame, 20));
    TEST_ASSERT_EQUAL_UINT32(20, hal.cursor);
    hal.bytes.insert(hal.bytes.end(), {0xAB, 0x70, 0xAA, 0x71, 255});
    link.sendHeartbeat();

    for (int n = 0; n < 20 && !link.receiveFrame(frame, 20); ++n)
    {
    }

    TEST_ASSERT_EQUAL_UINT8(SERIAL_MSG_HEARTBEAT, frame.messageId);
    TEST_ASSERT_EQUAL_UINT32(2, link.getLenErrors());
}

void test_capacity_guard_and_tx_backpressure()
{
    BulkHal hal;
    FFBSerialLink<BulkHal> link(hal);
    uint8_t data[4] = {1, 2, 3, 4};
    TEST_ASSERT_FALSE(link.sendRaw(9, nullptr, 1));
    TEST_ASSERT_FALSE(link.sendRaw(9, data, 65));
    TEST_ASSERT_FALSE(link.sendRaw(9, data, 4, SerialFrameFormat::Fixed));
    hal.full = true;
    TEST_ASSERT_FALSE(link.sendRaw(9, data, 4));
    TEST_ASSERT_EQUAL_UINT32(0, hal.bytes.size());
    hal.full = false;
    TEST_ASSERT_TRUE(link.sendRaw(9, data, 4));
    uint8_t out[3] = {7, 8, 9};
    TEST_ASSERT_EQUAL_UINT8(0, link.receive(out, 2));
    TEST_ASSERT_EQUAL_UINT8(9, out[2]);
    TEST_ASSERT_EQUAL_UINT32(1, link.getOutputErrors());
}

void test_receiver_reset_after_partial_frame()
{
    BulkHal hal;
    FFBSerialLink<BulkHal> link(hal);
    hal.bytes = {0xAA, 2, 64, 1};
    SerialFrameView frame;
    TEST_ASSERT_FALSE(link.receiveFrame(frame));
    link.resetReceiver();
    link.sendHeartbeat();
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_UINT8(SERIAL_MSG_HEARTBEAT, frame.messageId);
}

#ifdef SERIAL_TRANSPORT_BENCHMARK
template <SerialFraming Mode> void benchmark_transport(const char* name)
{
    BulkHal hal;
    FFBSerialLink<BulkHal, Mode> link(hal);
    constexpr size_t count = 300000;
    hal.bytes.reserve(count * (sizeof(PositionPayload) + 4));
    uint16_t pos[NUM_AXIS]{};
    pos[0] = 0xABAA;

    for (size_t i = 0; i < count; ++i)
    {
        pos[0] = uint16_t(i);
        link.sendPosition(pos);
    }

    const auto begin = std::chrono::steady_clock::now();
    size_t received = 0;
    uint64_t sum = 0, expected = 0;
    PositionPayload out;

    for (size_t i = 0; i < count; ++i)
        expected += uint16_t(i);

    while (link.receivePosition(out))
    {
        ++received;
        sum += out.position[0];
    }

    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - begin)
                        .count();
    TEST_ASSERT_EQUAL_UINT32(count, received);
    TEST_ASSERT_EQUAL_UINT64(expected, sum);
    std::printf("BENCH %s: %zu frames, %lld us, %.0f frames/s, %zu bulk reads\n", name, count,
                (long long)us, double(count) * 1000000 / double(us), hal.reads);
}

void test_parser_benchmark()
{
    benchmark_transport<SerialFraming::Variable>("variable");
    benchmark_transport<SerialFraming::Fixed>("fixed");
}
#endif
int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_legacy_wire_bytes_and_crc_vector);
    RUN_TEST(test_fixed_motor_frames_and_heartbeat);
    RUN_TEST(test_mixed_preserves_batched_frame_order_and_views);
    RUN_TEST(test_custom_fixed_layout_and_max_payload);
    RUN_TEST(test_255_byte_variable_frame_and_empty_id_zero);
    RUN_TEST(test_split_frames_and_crc_recovery);
    RUN_TEST(test_noise_work_budget_and_invalid_lengths);
    RUN_TEST(test_capacity_guard_and_tx_backpressure);
    RUN_TEST(test_receiver_reset_after_partial_frame);
#ifdef SERIAL_TRANSPORT_BENCHMARK
    RUN_TEST(test_parser_benchmark);
#endif
    return UNITY_END();
}
