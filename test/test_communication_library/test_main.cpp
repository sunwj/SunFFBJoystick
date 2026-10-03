#include <unity.h>
#include <vector>
#include <EmbeddedComm/SerialLink.h>
#include <EmbeddedComm/CanLink.h>

using namespace EmbeddedComm;

void setUp()
{
}

void tearDown()
{
}

struct ByteHal
{
    std::vector<uint8_t> bytes;
    size_t cursor = 0;

    size_t write(const uint8_t* data, size_t length)
    {
        bytes.insert(bytes.end(), data, data + length);
        return length;
    }

    int available()
    {
        return int(bytes.size() - cursor);
    }

    uint8_t read()
    {
        return bytes[cursor++];
    }
};

struct SensorLayout
{
    static constexpr uint16_t length(uint8_t id)
    {
        return id == 0x42 ? 3 : 0xFFFF;
    }
};

struct FrameHal
{
    CANFrame last;
    bool pending = false;

    bool send(const CANFrame& frame)
    {
        last = frame;
        pending = true;
        return true;
    }

    bool receive(CANFrame& frame)
    {
        if (!pending)
            return false;

        frame = last;
        pending = false;
        return true;
    }
};

void test_serial_without_motor_configuration()
{
    ByteHal hal;
    SerialLink<ByteHal> link(hal);
    const uint8_t payload[] = {0xAA, 0xAB, 0x19};
    TEST_ASSERT_TRUE(link.sendRaw(0, payload, sizeof(payload)));

    SerialFrameView frame;
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_UINT8(0, frame.messageId);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, frame.payload, sizeof(payload));
    TEST_ASSERT_FALSE(link.sendRaw(0x42, payload, sizeof(payload), SerialFrameFormat::Fixed));
}

void test_custom_fixed_protocol_without_axis_count()
{
    ByteHal hal;
    SerialLink<ByteHal, SerialFraming::Mixed, 8, SensorLayout> link(hal);
    const uint8_t payload[] = {1, 2, 3};
    TEST_ASSERT_TRUE(link.sendRaw(0x42, payload, 3, SerialFrameFormat::Fixed));
    TEST_ASSERT_FALSE(link.sendRaw(0x42, payload, 2, SerialFrameFormat::Fixed));

    SerialFrameView frame;
    TEST_ASSERT_TRUE(link.receiveFrame(frame, 2) == false);
    TEST_ASSERT_TRUE(link.receiveFrame(frame));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, frame.payload, 3);
    TEST_ASSERT_EQUAL_INT(int(SerialFrameFormat::Fixed), int(frame.format));
}

void test_raw_can_extended_remote_and_validation()
{
    FrameHal hal;
    CanLink<FrameHal> link(hal);
    CANFrame frame;
    frame.id = 0x1FFFFFFF;
    frame.extended = true;
    frame.remote = true;
    frame.length = 8;
    TEST_ASSERT_TRUE(link.send(frame));

    CANFrame received;
    TEST_ASSERT_TRUE(link.receive(received));
    TEST_ASSERT_EQUAL_UINT32(frame.id, received.id);
    TEST_ASSERT_TRUE(received.remote);
    TEST_ASSERT_TRUE(received.extended);

    frame.extended = false;
    TEST_ASSERT_FALSE(link.send(frame));
    frame.id = 0x123;
    frame.length = 9;
    TEST_ASSERT_FALSE(link.send(frame));
    hal.last = frame;
    hal.pending = true;
    received.id = 0x456;
    TEST_ASSERT_FALSE(link.receive(received));
    TEST_ASSERT_EQUAL_UINT32(0x456, received.id);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_serial_without_motor_configuration);
    RUN_TEST(test_custom_fixed_protocol_without_axis_count);
    RUN_TEST(test_raw_can_extended_remote_and_validation);
    return UNITY_END();
}
