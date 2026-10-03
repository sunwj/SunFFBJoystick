/**
 * Firmware entry point and FreeRTOS orchestration; loop is deleted after setup creates tasks.
 * Data flow: input source -> snapshot/metrics -> effect calculation -> force queue -> optional motors.
 * USB commands execute in the serialized service callback; input and PID state share an endpoint.
 * LCD and UART logs are diagnostic paths; motor enablement and timing require separate validation.
 *
 * Reading guide:
 * - setup() allocates shared resources before USB callbacks or worker tasks can use them.
 * - receive_position_task()/joystick_task() publish the latest input and motion metrics.
 * - force_calculation_task() combines those metrics with the host-owned effect pool.
 * - send_report_task()/send_force_task() deliver snapshots to USB and the motor transport.
 * - lcd_task()/timing_task() observe the pipeline at lower priority; the diagnostic
 *   console module owns the hardware UART0 writer and its bounded queue.
 *
 * Synchronization has two distinct roles: mutexes protect mutable model state,
 * while one-slot queues copy complete snapshots between producers and consumers.
 * Queue overwrites intentionally discard intermediate samples when a consumer lags.
 * All micros() timestamps are uint32_t; unsigned subtraction handles timer wrap
 * for the short intervals measured here. Timing maxima are observations, not a
 * guarantee that an arbitrary future workload will meet its deadline.
 */

#include <Arduino.h>
#include <FreeRTOS.h>
#include <USB.h>
#include <USBHID.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "device/usbd.h"
#include "device/usbd_pvt.h"
#include <memory.h>
#include <algorithm>
#include <atomic>
#include "constants.h"

#if ENABLE_HIL_DIAGNOSTICS
#include "../diagnostics/hil_diagnostics.h"
#endif

// Framework CDC auto-registration must remain disabled; Serial is hardware UART0.
static_assert(ARDUINO_USB_CDC_ON_BOOT == 0, "Firmware requires a HID-only USB device");
#include "diagnostic_console.h"

#if ENABLE_LCD
#include <TFT_eSPI.h>

#endif
#include "ffb_report_types.h"
#include "ffb_report_descriptor.h"
#include "ffb_report_handler.h"
#include "ffb_report_dispatch.h"
#include "ffb_device_input.h"
#include "ffb_force_calculator.h"
#include "motor_protocol/motor_payload.h"

#if MOTOR_TRANSPORT == 1
#include <EmbeddedComm/Esp32TwaiHal.h>
#include "motor_protocol/can_link.h"

#else
#include <EmbeddedComm/ArduinoSerialHal.h>
#include "motor_protocol/serial_link.h"

#endif
#include "realtime_timing.h"

// The 1 ms force loop requires the ESP Arduino core's 1 kHz RTOS tick.
static_assert(configTICK_RATE_HZ == 1000, "Force scheduling requires a 1 ms RTOS tick");
static_assert(FORCE_TASK_PERIOD_MS == 1 || FORCE_TASK_PERIOD_MS == 2,
              "Force rate must be 500 or 1000 Hz");

// 8N1: ten wire bits per byte, four framing bytes. TX/RX are full duplex.
#if MOTOR_TRANSPORT == 0
static_assert((4 + sizeof(SunFFB::ForcePayload)) * 10 * 500 < UART1_BAUD,
              "Motor UART cannot carry 500 Hz force frames");
#endif
// S2 has one core; S3 keeps model computation on core 1 and protocol I/O on core 0.
#if CONFIG_FREERTOS_UNICORE
constexpr BaseType_t applicationCore = 0;

#else
constexpr BaseType_t applicationCore = 1;
#endif
// Leave the USB service, timer service and force task ahead of periodic I/O.
constexpr UBaseType_t periodicIOPriority = 2;

static void createFirmwareTask(TaskFunction_t entry, const char* name, uint32_t stackBytes,
                               UBaseType_t priority, BaseType_t core)
{
    // ESP-IDF's pinned-task API takes stack size in bytes. Fail at initialization
    // if a task cannot be created rather than running an incomplete control pipeline.
    const BaseType_t result =
        xTaskCreatePinnedToCore(entry, name, stackBytes, nullptr, priority, nullptr, core);
    configASSERT(result == pdPASS);
}

#if NUM_AXIS == 1
constexpr uint8_t joystickPins[NUM_AXIS] = {AXIS0_ADC_PIN};
#elif NUM_AXIS == 2
constexpr uint8_t joystickPins[NUM_AXIS] = {AXIS0_ADC_PIN, AXIS1_ADC_PIN};
#elif NUM_AXIS == 3
constexpr uint8_t joystickPins[NUM_AXIS] = {AXIS0_ADC_PIN, AXIS1_ADC_PIN, AXIS2_ADC_PIN};
#endif

#if !USE_EXTERNAL_POSITION
static_assert(AXIS0_ADC_PIN != 19 && AXIS0_ADC_PIN != 20, "ADC pin conflicts with USB");
#if NUM_AXIS >= 2
static_assert(AXIS1_ADC_PIN != 19 && AXIS1_ADC_PIN != 20, "ADC pin conflicts with USB");
#endif
#if NUM_AXIS == 3
static_assert(AXIS2_ADC_PIN != 19 && AXIS2_ADC_PIN != 20, "ADC pin conflicts with USB");
#endif
#endif

#if MOTOR_TRANSPORT == 1 && !USE_EXTERNAL_POSITION
static_assert(CAN_TX_PIN != AXIS0_ADC_PIN && CAN_RX_PIN != AXIS0_ADC_PIN,
              "CAN conflicts with ADC axis 0");
#if NUM_AXIS >= 2
static_assert(CAN_TX_PIN != AXIS1_ADC_PIN && CAN_RX_PIN != AXIS1_ADC_PIN,
              "CAN conflicts with ADC axis 1");
#endif
#if NUM_AXIS == 3
static_assert(CAN_TX_PIN != AXIS2_ADC_PIN && CAN_RX_PIN != AXIS2_ADC_PIN,
              "CAN conflicts with ADC axis 2");
#endif
#endif
#if MOTOR_TRANSPORT == 1 && defined(USE_BUTTON)
static_assert(CAN_TX_PIN != SW_PIN && CAN_RX_PIN != SW_PIN, "CAN conflicts with button GPIO");
#endif

uint16_t coordOffsets[NUM_AXIS] = {0};
// Descriptor bytes are selected at compile time with NUM_AXIS. The descriptor,
// packed report types and host client must agree on IDs and payload sizes.
static const uint8_t ffbReportDescriptor[] PROGMEM = {FFB_REPORT_DESCRIPTOR_CONTENT};

uint16_t hid_get_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer,
                                 uint16_t reqlen);
void hid_set_report_callback(uint8_t reportId, hid_report_type_t reportType, const uint8_t* buffer,
                             uint16_t bufSize);

struct PositionSample
{
    // Carry raw encoder values and arrival time together; sequence distinguishes fresh samples from rereads.
    uint16_t position[NUM_AXIS];
    uint32_t
        receivedUs; // Timestamp after decoding a valid transport payload, not a wire timestamp.
    uint32_t sequence;
};

struct JoystickSample
{
    // Keep source timestamps with host reports to measure sample age at USB submission.
    SunFFB::JoystickInputReportData report;
    uint32_t receivedUs;
    uint32_t sequence;
};

// Cadence/work counters and release-to-completion deadline counters answer
// different questions. USB submission and actual completion are also separate
// events; only completed fresh position reports advance freshPositionTiming.
SunFFB::TimingStream forceTiming(FORCE_TASK_PERIOD_MS * 1000);
SunFFB::ForceDeadlineStream forceDeadlines(FORCE_TASK_PERIOD_MS * 1000);
SunFFB::TimingStream positionRxTiming(2000), inputTiming(2000);
SunFFB::TimingStream usbSubmitTiming(2000), usbCompleteTiming(2000);
SunFFB::TimingStream freshPositionTiming(2000), motorTxTiming(2000);

#if ENABLE_LCD
// volatile alone does not synchronize tasks on different cores. Protect the
// latest LCD durations with a short critical section, independent of model locks.
struct ProcessingTimeSnapshot
{
    uint32_t reportUs = 0;
    uint32_t effectUs = 0;
};
static SunFFB::TimingLock processingTimesLock;
static ProcessingTimeSnapshot processingTimes;
#endif

// Mailbox ownership:
// gPositions: transport RX -> joystick task (receive consumes the latest sample).
// gForces: force task -> motor TX and LCD (peek leaves the snapshot available).
// gJoystickReportData: joystick task -> USB sender (peek permits submission retry).
QueueHandle_t gPositions;
QueueHandle_t gForces;
QueueHandle_t gJoystickReportData;

#if MOTOR_TRANSPORT == 1
EmbeddedComm::Esp32TwaiHal canHal;
SunFFB::MotorCANLink<EmbeddedComm::Esp32TwaiHal> motorLink(canHal, {CAN_FORCE_ID, CAN_POSITION_ID,
                                                                    CAN_HEARTBEAT_ID});
#else
HardwareSerial comSerial(1);
EmbeddedComm::ArduinoSerialHal serialHal(comSerial);
SunFFB::MotorSerialLink<EmbeddedComm::ArduinoSerialHal,
                        static_cast<SunFFB::SerialFraming>(SERIAL_FRAMING_MODE)>
    motorLink(serialHal);

#endif

#if ENABLE_LCD
TFT_eSPI lcd = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&lcd);
#endif

class FFBUSBHIDDevice : public USBHIDDevice
{
    // The Arduino USBHID adapter forwards callbacks; the shared handler owns protocol semantics.

    public:
    uint16_t _onGetDescriptor(uint8_t* buffer) override
    {
        memcpy(buffer, ffbReportDescriptor, sizeof(ffbReportDescriptor));
        return sizeof(ffbReportDescriptor);
    }

    uint16_t _onGetFeature(uint8_t reportId, uint8_t* buffer, uint16_t length) override
    {
        return hid_get_report_callback(reportId, HID_REPORT_TYPE_FEATURE, buffer, length);
    }

    void _onSetFeature(uint8_t reportId, const uint8_t* buffer, uint16_t length) override
    {
        hid_set_report_callback(reportId, HID_REPORT_TYPE_FEATURE, buffer, length);
    }

    void _onOutput(uint8_t reportId, const uint8_t* buffer, uint16_t length) override
    {
        hid_set_report_callback(reportId, HID_REPORT_TYPE_OUTPUT, buffer, length);
    }
};

// These objects live for the entire firmware lifetime. The input mutex protects
// ffbDeviceInput; the handler mutex protects ffbHandler, including its effect pool.
// The calculator is used only by the force task while both model locks are held.
USBHID usb_hid;
FFBUSBHIDDevice ffbUsbHidDevice;
SunFFB::FFBDeviceInput ffbDeviceInput;
SunFFB::FFBReportHandler ffbHandler;
SunFFB::FFBForceCalculator ffbForceCalculator;

struct DeferredHIDReport
{
    // Persistent storage bridges the sender task and a queued USB-service callback.
    // owner receives an acceptance notification; this is not a wire-completion signal.
    TaskHandle_t owner;
    uint8_t id;
    uint16_t length;
    uint8_t bytes[sizeof(SunFFB::JoystickInputReportData)];
    uint32_t receivedUs;
    bool fresh;
};
static DeferredHIDReport deferredReport{};
// The single HID IN endpoint has one in-flight transfer. The completion hook
// attributes that transfer using the freshness flag saved when it was accepted.
static bool submittedPositionFresh;

static void submit_hid_report(void*)
{
#if ENABLE_HIL_DIAGNOSTICS
    usbServiceHandle.store(xTaskGetCurrentTaskHandle(), std::memory_order_relaxed);
    hidCalls.fetch_add(1, std::memory_order_relaxed);
#endif
    // Submit from TinyUSB's service context, not concurrently from another core.
    // The service must never wait for its own completion callback.
    const bool accepted =
        tud_hid_n_ready(0) &&
        tud_hid_n_report(0, deferredReport.id, deferredReport.bytes, deferredReport.length);
    if (accepted && deferredReport.id == REPORT_ID_JOYSTICK)
    {
        submittedPositionFresh = deferredReport.fresh;
        const uint32_t nowUs = micros();
        usbSubmitTiming.record(nowUs, nowUs - deferredReport.receivedUs);
    }
    xTaskNotify(deferredReport.owner, accepted ? 1 : 2, eSetValueWithOverwrite);
}

void hid_report_complete_hook(const uint8_t* report, uint16_t length)
{
    // Called by the compatibility wrapper after TinyUSB completes an IN transfer.
    // The completed buffer includes its report ID; PID state traffic is excluded
    // from joystick cadence and fresh-position measurements.
    if (length && report[0] == REPORT_ID_JOYSTICK)
    {
        usbCompleteTiming.record(micros());
        if (submittedPositionFresh)
        {
            freshPositionTiming.record(micros());
        }
    }
}

static bool submitReport(uint8_t id, const void* payload, uint16_t length, uint32_t receivedUs = 0,
                         bool fresh = false)
{
    // Only send_report_task owns this persistent slot. Await submission, not USB
    // wire completion; it cannot be reused until the queued callback has consumed it.
    configASSERT(length <= sizeof(deferredReport.bytes));
    deferredReport.owner = xTaskGetCurrentTaskHandle();
    deferredReport.id = id;
    deferredReport.length = length;
    deferredReport.receivedUs = receivedUs;
    deferredReport.fresh = fresh;
    memcpy(deferredReport.bytes, payload, length);
    usbd_defer_func(submit_hid_report, nullptr, false);
    // Values 1/2 distinguish accepted/rejected submission. Waiting here also
    // prevents the next report from overwriting the deferred callback's input.
    uint32_t result;
    xTaskNotifyWait(0, UINT32_MAX, &result, portMAX_DELAY);
    return result == 1;
}

TaskHandle_t forceCalculationTaskHandle;
SemaphoreHandle_t forceStartGate;
static uint32_t forceReleaseUs;

static void force_timer_callback(void*)
{
    // Runs in esp_timer's task context, not an interrupt. Advance a nominal
    // release phase instead of scheduling the next job relative to completion.
    constexpr uint32_t periodUs = FORCE_TASK_PERIOD_MS * 1000;
    forceReleaseUs += periodUs;
    const uint32_t nowUs = micros();
    const uint32_t behindUs = nowUs - forceReleaseUs;
    // A difference in the upper half of uint32_t represents a future release
    // under wraparound arithmetic, so it must not be interpreted as huge lateness.
    if (behindUs >= periodUs && behindUs < 0x80000000U)
    {
        // Coalesce delayed releases; never replay a backlog of stale force samples.
        forceReleaseUs += (behindUs / periodUs) * periodUs;
    }
    // The notification is a latest-release mailbox: a busy force task receives
    // the newest phase timestamp, and deadline accounting detects skipped phases.
    xTaskNotify(forceCalculationTaskHandle, forceReleaseUs, eSetValueWithOverwrite);
}
SemaphoreHandle_t semaphoreFFBDeviceInput;
SemaphoreHandle_t semaphoreFFBReportHandler;

uint16_t hid_get_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer,
                                 uint16_t reqlen)
{
    // Only the allocation result and pool description are readable Feature
    // reports here. Return zero for unsupported requests or undersized buffers.
    // The callback payload excludes the report ID; the USB layer handles that ID.
    switch (report_type)
    {
        case HID_REPORT_TYPE_FEATURE:
            switch (report_id)
            {
                // Block Load Request
                case REPORT_ID_BLOCK_LOAD_REPORT:
                {
                    SunFFB::BlockLoadReportData data{};
                    if (!buffer || reqlen < sizeof(data))
                        return 0;

                    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
                    data = *ffbHandler.get_block_load_report_data();
                    xSemaphoreGive(semaphoreFFBReportHandler);
                    memcpy(buffer, &data, sizeof(data));

                    return sizeof(data);
                }

                // Pool size, max simultaneous effects, etc.
                case REPORT_ID_POOL_REPORT:
                {
                    SunFFB::PoolReportData data{};
                    if (!buffer || reqlen < sizeof(data))
                        return 0;

                    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
                    data = *ffbHandler.get_pool_report_data();
                    xSemaphoreGive(semaphoreFFBReportHandler);
                    memcpy(buffer, &data, sizeof(data));

                    return sizeof(data);
                }

                default:
                    break;
            }

            break;

        default:
            break;
    }

    return 0;
}

void hid_set_report_callback(uint8_t reportId, hid_report_type_t reportType, const uint8_t* buffer,
                             uint16_t bufSize)
{
    // Validate type, ID and payload size before interpreting packed host data.
    // The dispatcher implements protocol semantics; this entry point supplies
    // serialization and timing around changes to the shared effect model.
    const uint32_t startTime = micros();
    if (!SunFFB::valid_output_report(reportId, reportType, buffer, bufSize))
        return;

#if ENABLE_HIL_DIAGNOSTICS
    commandStarts.fetch_add(1, std::memory_order_relaxed);
#endif
    // TinyUSB serializes callbacks. Execute bounded command handling here rather
    // than suspending its service task for a second task on the same core.
    // Dispatch copies packed reports locally and ordering is preserved before GET.
    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
    SunFFB::dispatch_output_report(ffbHandler, reportId, reportType, buffer, bufSize);
    xSemaphoreGive(semaphoreFFBReportHandler);
#if ENABLE_HIL_DIAGNOSTICS
    commandEnds.fetch_add(1, std::memory_order_relaxed);
#endif
#if ENABLE_LCD
    const uint32_t elapsedUs = micros() - startTime;
    SunFFB::TimingGuard guard(processingTimesLock);
    processingTimes.reportUs = elapsedUs;
#endif
}

#if ENABLE_LCD
static void push_lcd_sprite_bounded()
{
    // Split LCD updates into bounded row groups so the force task can run between transfers.
    constexpr int16_t rowsPerTransfer = 4;
    auto* pixels = static_cast<uint16_t*>(sprite.getPointer());
    configASSERT(pixels != nullptr);
    const bool oldSwapBytes = lcd.getSwapBytes();
    // Sprite pixels already use the byte order expected by this raw push path.
    // Restore the previous display setting so other TFT operations retain theirs.
    lcd.setSwapBytes(false);

    for (int16_t row = 0; row < TFT_H; row += rowsPerTransfer)
    {
        const int16_t rows = std::min<int16_t>(rowsPerTransfer, TFT_H - row);
#if ENABLE_HIL_DIAGNOSTICS
        lcdStage.store(2, std::memory_order_relaxed);
#endif
        lcd.pushImage(TFT_X, TFT_Y + row, TFT_W, rows, pixels + row * TFT_W);
#if ENABLE_HIL_DIAGNOSTICS
        lcdStage.store(1, std::memory_order_relaxed);
#endif
        taskYIELD();
    }
    lcd.setSwapBytes(oldSwapBytes);
}

void lcd_task(void* params)
{
    // Render from snapshots, so a slow display never holds a model mutex during
    // text drawing or SPI transfers. A timed-out snapshot keeps the previous
    // axis/effect display; it does not delay the control loop indefinitely.
    // Hold shared locks only for snapshots; release them before rendering and SPI display transfers.
    // gForces is a one-slot latest-value queue; peek does not consume motor output.
    TickType_t wakeupTime = xTaskGetTickCount();

    while (true)
    {
        int32_t forces[NUM_AXIS] = {0};
        xQueuePeek(gForces, forces, 0);
#if ENABLE_HIL_DIAGNOSTICS
        lcdStage.store(1, std::memory_order_relaxed);
#endif

        sprite.fillSprite(TFT_BLACK);
        sprite.drawRect(0, 0, 40, 40, TFT_RED);

        // snapshot axis data under semaphore for consistent reads
        static int16_t axisSnapshot[NUM_AXIS] = {0};
        if (xSemaphoreTake(semaphoreFFBDeviceInput, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            memcpy(axisSnapshot, (const int16_t*)ffbDeviceInput.inputData.axis,
                   sizeof(axisSnapshot));
            xSemaphoreGive(semaphoreFFBDeviceInput);
        }

        // Map signed full-scale position into the upper 40x40 plot, leaving a
        // two-pixel margin so the marker stays inside the red border.
        uint8_t coords[NUM_AXIS];
#pragma unroll
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            uint8_t c = axisSnapshot[i] / float(USB_AXIS_MAX_ABSOLUTE) * 20 + 20;
            c = c < 2 ? 2 : c;
            c = c > 38 ? 38 : c;
            coords[i] = c;
        }

#if NUM_AXIS == 1
        sprite.fillSmoothCircle(coords[0], 20, 2, TFT_GREEN, TFT_BLACK);
#elif NUM_AXIS >= 2
        sprite.fillSmoothCircle(coords[0], coords[1], 2, TFT_GREEN, TFT_BLACK);
#endif

        ProcessingTimeSnapshot times;
        {
            SunFFB::TimingGuard guard(processingTimesLock);
            times = processingTimes;
        }
        sprite.setCursor(40, 0);
        sprite.printf("Report time: %lu us", (unsigned long)times.reportUs);
        sprite.setCursor(40, 11);
        if (times.effectUs > FORCE_TASK_MAX_TIME_US)
        {
            sprite.setTextColor(TFT_RED);
            sprite.printf("Effect time: %lu us !", (unsigned long)times.effectUs);
        }
        else
        {
            sprite.setTextColor(TFT_WHITE);
            sprite.printf("Effect time: %lu us", (unsigned long)times.effectUs);
        }

        sprite.setTextColor(TFT_WHITE);
#if NUM_AXIS == 1
        sprite.setCursor(40, 23);
        sprite.printf("AX: %d", axisSnapshot[0]);
#elif NUM_AXIS >= 2
        sprite.setCursor(40, 23);
        sprite.printf("AX: %d", axisSnapshot[0]);
        sprite.setCursor(100, 23);
        sprite.printf("AY: %d", axisSnapshot[1]);
#endif

// The third axis is shown as text; the XY plot retains its two panels.
#pragma unroll
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            const float center = i == 1 ? 60.f : 20.f;
            coords[i] = std::clamp(forces[i] / float(USB_MAX_MAGNITUDE) * 20 + center,
                                   center - 18.f, center + 18.f);
        }

        sprite.drawRect(0, 40, 40, 40, TFT_RED);
#if NUM_AXIS == 1
        sprite.fillSmoothCircle(coords[0], 60, 2, TFT_GREEN, TFT_BLACK);
        sprite.drawWideLine(20, 60, coords[0], 60, 2, TFT_GREEN, TFT_BLACK);
#elif NUM_AXIS >= 2
        sprite.fillSmoothCircle(coords[0], coords[1], 2, TFT_GREEN, TFT_BLACK);
        sprite.drawWideLine(20, 60, coords[0], coords[1], 2, TFT_GREEN, TFT_BLACK);
#endif

#if NUM_AXIS == 1
        sprite.setCursor(40, 35);
        sprite.printf("FX: %d", forces[0]);
#elif NUM_AXIS >= 2
        sprite.setCursor(40, 35);
        sprite.printf("FX: %d", forces[0]);
        sprite.setCursor(100, 35);
        sprite.printf("FY: %d", forces[1]);
#endif

#if NUM_AXIS == 3
        sprite.setCursor(40, 70);
        sprite.printf("Z:%d F:%d", axisSnapshot[2], forces[2]);
#endif
        const uint32_t uptimeSeconds = millis() / 1000U;
        // Hours may exceed two digits; minutes and seconds wrap within 0..59.
        // This is boot uptime based on millis(), including its natural wraparound.
        const uint32_t uptimeHours = uptimeSeconds / 3600U;
        const uint32_t uptimeMinutes = (uptimeSeconds / 60U) % 60U;
        const uint32_t uptimeSecondsPart = uptimeSeconds % 60U;
        sprite.setCursor(100, 70);
        sprite.printf("%02lu:%02lu:%02lu", uptimeHours, uptimeMinutes, uptimeSecondsPart);

        // snapshot effect states under semaphore for consistent reads
        static uint8_t effectStates[MAX_EFFECTS] = {0};
        if (xSemaphoreTake(semaphoreFFBReportHandler, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            for (uint8_t i = 0; i < MAX_EFFECTS; ++i)
                effectStates[i] = ffbHandler.get_all_effect_blocks()[i].state;
            xSemaphoreGive(semaphoreFFBReportHandler);
        }

        // One marker per effect slot: green is free, blue allocated, red playing.
        for (uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            uint16_t color = TFT_BLACK;
            if (EFFECT_STATE_FREE == effectStates[i])
                color = TFT_GREEN;
            else if (effectStates[i] & EFFECT_STATE_ALLOCATED)
            {
                color = TFT_BLUE;
                if (effectStates[i] & EFFECT_STATE_PLAYING)
                    color = TFT_RED;
            }

            sprite.fillSmoothCircle(44 + i % 14 * 8, 48 + 8 * int(i / 14), 4, color, TFT_BLACK);
        }

#if ENABLE_HIL_DIAGNOSTICS
        lcdStage.store(2, std::memory_order_relaxed);
#endif
        push_lcd_sprite_bounded();
#if ENABLE_HIL_DIAGNOSTICS
        lcdStage.store(0, std::memory_order_relaxed);
#endif

        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(LCD_TASK_PERIOD_MS));
    }
}

#endif // ENABLE_LCD

void joystick_task(void* params)
{
    // Build flags select input: external position waits for new sequence values; ADC samples periodically.
    // Convert to signed HID coordinates before FFBDeviceInput; all sources share subsequent filtering.
    TickType_t wakeupTime = xTaskGetTickCount();
    uint32_t sequence = 0;

    while (true)
    {
        uint32_t receivedUs;
        int16_t coords[NUM_AXIS];
#if USE_EXTERNAL_POSITION
        PositionSample sample;
        xQueueReceive(gPositions, &sample, portMAX_DELAY);
        // External feedback drives this task by arrival. The one-slot queue
        // coalesces bursts; a sample is processed once rather than polled again.
        receivedUs = sample.receivedUs;
        sequence = sample.sequence;

        // Encoder center is 32768. Clamp before narrowing to the symmetric HID
        // range, so encoder value zero maps to -32767 rather than -32768.
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
            coords[i] = std::clamp<int32_t>(int32_t(sample.position[i]) - 32768, -32767, 32767);
#else
        receivedUs = micros();
        // ADC mode establishes a new source sequence on each periodic acquisition.
        ++sequence;
#pragma unroll
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            coords[i] = analogRead(joystickPins[i]) - coordOffsets[i];
            coords[i] = std::clamp(coords[i], int16_t(-ADC_CLAMP), int16_t(ADC_CLAMP));
            coords[i] = coords[i] / ADC_SCALE * USB_AXIS_MAX_ABSOLUTE;
        }

#endif
#ifdef USE_BUTTON
        uint8_t btnState = digitalRead(SW_PIN) ? 0 : 1;
#endif

        // Update filters/derivatives and copy the HID report under one lock.
        // Force computation therefore sees a consistent set of motion metrics.
        xSemaphoreTake(semaphoreFFBDeviceInput, portMAX_DELAY);
#ifdef USE_BUTTON
        ffbDeviceInput.update_buttons(btnState);
#endif
        ffbDeviceInput.update_axis(coords);
        JoystickSample output{};
        memcpy(&output.report, (const void*)&ffbDeviceInput.inputData, sizeof(output.report));
        output.receivedUs = receivedUs;
        output.sequence = sequence;
        xSemaphoreGive(semaphoreFFBDeviceInput);
        xQueueOverwrite(gJoystickReportData, &output);
        inputTiming.record(micros());
#if !USE_EXTERNAL_POSITION
        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(JOYSTICK_TASK_PERIOD_MS));
#endif
    }
}

void force_calculation_task(void* params)
{
    // Lock order is input -> effect pool; other paths holding both locks must preserve that order.
    // Publish only the latest computed force so slow consumers do not accumulate stale control output.
    forceCalculationTaskHandle = xTaskGetCurrentTaskHandle();
    // Start deadlines only after setup has created every peer task and queue.
    xSemaphoreTake(forceStartGate, portMAX_DELAY);
    // Peer-task creation may preempt this task during setup. The startup gate
    // excludes that initialization work from the periodic release phase.
    esp_timer_handle_t forceTimer;
    esp_timer_create_args_t timerArgs{};
    timerArgs.callback = force_timer_callback;
    timerArgs.name = "ffb_force";
    timerArgs.dispatch_method = ESP_TIMER_TASK;
    timerArgs.skip_unhandled_events = true;
    ESP_ERROR_CHECK(esp_timer_create(&timerArgs, &forceTimer));
    forceReleaseUs = micros();
    ESP_ERROR_CHECK(esp_timer_start_periodic(forceTimer, FORCE_TASK_PERIOD_MS * 1000));

    while (true)
    {
        uint32_t releaseUs;
        xTaskNotifyWait(0, UINT32_MAX, &releaseUs, portMAX_DELAY);
        // releaseUs is the nominal deadline origin; startTime is when this task
        // actually runs. Keeping both exposes scheduler wake-up latency.
        const uint32_t startTime = micros();
        int32_t forces[NUM_AXIS] = {0};

        xSemaphoreTake(semaphoreFFBDeviceInput, portMAX_DELAY);
        xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
        // These timestamps separate mutex wait from computation and publication.
        // Keep each event's tuple together; independent maxima may be from
        // different cycles and cannot be added to reconstruct one slow cycle.
        const uint32_t lockedUs = micros();
        const uint32_t context = uint32_t(ffbHandler.deviceState);
        ffbForceCalculator.force_calculator(ffbHandler, ffbDeviceInput, (int32_t*)forces);
        const uint32_t computedUs = micros();
        xSemaphoreGive(semaphoreFFBReportHandler);
        xSemaphoreGive(semaphoreFFBDeviceInput);

        xQueueOverwrite(gForces, forces);

        uint32_t endTime = micros();
        const uint32_t effectProcessTime = endTime - startTime;
        // The duration shown on the LCD excludes release-to-start latency. Deadline accounting
        // includes it and records skipped releases as well as completed-job misses.
        forceTiming.record(endTime, effectProcessTime);
        forceDeadlines.record_detail(releaseUs, startTime, lockedUs, computedUs, endTime,
                                     context
#if ENABLE_HIL_DIAGNOSTICS
                                         | (lcdStage.load(std::memory_order_relaxed) << 8)
#endif
        );
#if ENABLE_LCD
        {
            SunFFB::TimingGuard guard(processingTimesLock);
            processingTimes.effectUs = effectProcessTime;
        }
#endif

        // A missed deadline must not turn the highest periodic task into a
        // busy loop, especially when all firmware tasks share one CPU.
        if (effectProcessTime >= FORCE_TASK_PERIOD_MS * 1000)
        {
            vTaskDelay(1);
        }
    }
}

void send_report_task(void* params)
{
    // One task owns HID IN; positions take priority and PID states use spare endpoint slots.
    // Failed submission neither acknowledges state nor advances position scheduling, allowing retry.
    TickType_t wakeupTime = xTaskGetTickCount();
    SunFFB::ReportSchedule schedule(POSITION_REPORT_PERIOD_MS * 1000, micros());
    uint32_t lastSequence = 0;
    bool hasSequence = false;

    while (true)
    {
        const uint32_t nowUs = micros();
        if (usb_hid.ready())
        {
            JoystickSample sample;
            const bool hasSample = xQueuePeek(gJoystickReportData, &sample, 0) == pdTRUE;
            const bool fresh = hasSample && (!hasSequence || sample.sequence != lastSequence);
#if USE_EXTERNAL_POSITION

            // Forward arrivals, not a second free-running 500 Hz sampler.
            // No duplicate reports can masquerade as fresh motor positions.
            const bool positionDue = fresh;
#else
            const bool positionDue = schedule.due(nowUs);
#endif
            if (positionDue)
            {
                if (hasSample)
                {
                    if (submitReport(REPORT_ID_JOYSTICK, &sample.report, sizeof(sample.report),
                                     sample.receivedUs, fresh))
                    {
                        schedule.sent(nowUs);
                        if (fresh)
                        {
                            lastSequence = sample.sequence;
                            hasSequence = true;
                        }
                    }
                    else
                        usbSubmitTiming.failed();
                }
            }
            else
            {
                SunFFB::PIDStateReportData data{};
                uint32_t revision = 0;
                bool pending = false;
                if (xSemaphoreTake(semaphoreFFBReportHandler, 0) == pdTRUE)
                {
                    // Snapshot both content and revision. A later host command
                    // may change state while this snapshot is being submitted.
                    pending = ffbHandler.peek_pid_state_report(data, revision);
                    xSemaphoreGive(semaphoreFFBReportHandler);
                }

                // Never wait for USB completion while holding an effects mutex.
                // Its command callback needs that mutex before servicing completion.
                if (pending && submitReport(REPORT_ID_PID_STATE, &data, sizeof(data)))
                {
                    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
                    // Revision-aware acknowledgement must not clear a newer
                    // state change that occurred after the snapshot was taken.
                    ffbHandler.acknowledge_pid_state_report(data, revision);
                    xSemaphoreGive(semaphoreFFBReportHandler);
                }
            }
        }

        // Preserve periodic pacing when on time; after an overrun, yield and
        // restart the tick phase rather than spinning through missed periods.
        if (xTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(1)) == pdFALSE)
        {
            vTaskDelay(1);
            wakeupTime = xTaskGetTickCount();
        }
    }
}

void send_force_task(void* params)
{
    // Attempt latest-force submission at 500 Hz; sendForce success means transport acceptance only.
    // Do not wait for UART drain or CAN wire completion on the core shared with USB.
    TickType_t wakeupTime = xTaskGetTickCount();
#if ENABLE_HIL_DIAGNOSTICS && MOTOR_TRANSPORT == 0
    uint32_t lastHealthMs = 0;
#endif

    while (true)
    {
        int32_t forces[NUM_AXIS];
        xQueuePeek(gForces, forces, portMAX_DELAY);
        // Peek blocks until the first force exists, then resends the newest
        // snapshot at the transport period even if input has not changed.
        const uint32_t startUs = micros();
        if (motorLink.sendForce(forces))
            motorTxTiming.record(micros(), micros() - startUs);
        else
            motorTxTiming.failed();

#if ENABLE_HIL_DIAGNOSTICS && MOTOR_TRANSPORT == 0
        if (uint32_t(millis() - lastHealthMs) >= 1000)
        {
            lastHealthMs = millis();
            // HIL telemetry shares UART1 and its TX owner. Its bounded records
            // are emitted here so a second task cannot interleave motor frames.
            emit_hil_diagnostics(motorLink, forceDeadlines, forceCalculationTaskHandle,
                                 lastHealthMs);
        }
#endif

        if (xTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(SEND_FORCE_TASK_PERIOD_MS)) == pdFALSE)
        {
            // Do not replay missed output periods in a tight catch-up loop.
            vTaskDelay(1);
            wakeupTime = xTaskGetTickCount();
        }
    }
}

void receive_position_task(void* params)
{
    // Process at most eight frames per round and always yield; sustained input must not starve other tasks.
    // Rejected UART/CAN frames do not update the position queue; old snapshots cannot count as fresh feedback.
    uint32_t sequence = 0;

    while (true)
    {
#if MOTOR_TRANSPORT == 1
        // A bus recovery invalidates the receiver's prior sequence history.
        if (canHal.service())
            motorLink.resetReceiver();
#endif

        // Bound RX work on a shared CPU; CAN also services recovery without position input.
        for (uint8_t frame = 0; frame < 8; ++frame)
        {
            SunFFB::PositionPayload payload;
#if MOTOR_TRANSPORT == 1
            const auto result = motorLink.pollPosition(payload, micros());
            if (result == SunFFB::CANReceiveResult::Empty)
                break;
            if (result != SunFFB::CANReceiveResult::Position)
                continue;
#if !USE_CAN_POSITION
            continue;
#endif
#else
            if (!motorLink.hasPendingInput())
                break;
            if (!motorLink.receivePosition(payload))
                continue;
#endif
            PositionSample sample{};
            // Only a validated position payload gets a timestamp and sequence.
            // Transport errors/other CAN IDs cannot refresh the input mailbox.
            memcpy(sample.position, payload.position, sizeof(sample.position));
            sample.receivedUs = micros();
            sample.sequence = ++sequence;
            positionRxTiming.record(sample.receivedUs);
            xQueueOverwrite(gPositions, &sample);
        }

        vTaskDelay(pdMS_TO_TICKS(RECV_POSITION_TASK_PERIOD_MS));
    }
}

#if ENABLE_TIMING_DIAGNOSTICS
void timing_task(void*)
{
    // Print counter deltas at low priority; snapshot first and never print inside counter critical sections.
    SunFFB::TimingStream* streams[] = {&forceTiming,     &positionRxTiming,  &inputTiming,
                                       &usbSubmitTiming, &usbCompleteTiming, &freshPositionTiming,
                                       &motorTxTiming};
    const char* names[] = {"force", "rx", "input", "usb_submit", "usb_done", "fresh", "motor_tx"};
    uint32_t previous[7]{};
    uint32_t lastUs = micros();

    while (true)
    {
#if MOTOR_TRANSPORT == 1
        const auto can = canHal.stats();
        SunFFB::diagnostic_printf(
            "CAN accepted=%lu rejected=%lu success_alerts=%lu failure_alerts=%lu bus_off=%lu recovery=%lu overflow_alerts=%lu\n",
            (unsigned long)can.accepted, (unsigned long)can.rejected,
            (unsigned long)can.successAlerts, (unsigned long)can.failureAlerts,
            (unsigned long)can.busOffAlerts, (unsigned long)can.recoveries,
            (unsigned long)can.overflowAlerts);
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
        const uint32_t nowUs = micros();
        const uint32_t windowUs = nowUs - lastUs;
        lastUs = nowUs;

        for (uint8_t i = 0; i < 7; ++i)
        {
            const auto stat = streams[i]->take_window();
            // count is cumulative, while window maxima/failures are drained by
            // take_window(). Rate uses the actual elapsed reporting interval.
            SunFFB::diagnostic_printf(
                "TIMING %s hz=%.1f max_gap_us=%lu gap_over_budget=%lu max_work_us=%lu fail=%lu\n",
                names[i], float(stat.count - previous[i]) * 1000000.f / windowUs,
                (unsigned long)stat.maxGapUs, (unsigned long)stat.overBudget,
                (unsigned long)stat.maxWorkUs, (unsigned long)stat.failures);
            previous[i] = stat.count;
        }

        const auto deadline = forceDeadlines.take_window();
        SunFFB::diagnostic_printf(
            "DEADLINE force count=%lu wake_us=%lu lock_us=%lu elapsed_us=%lu missed=%lu skipped=%lu\n",
            (unsigned long)deadline.count, (unsigned long)deadline.maxWakeUs,
            (unsigned long)deadline.maxLockUs, (unsigned long)deadline.maxElapsedUs,
            (unsigned long)deadline.missed, (unsigned long)deadline.skipped);
        SunFFB::diagnostic_printf(
            "HEALTH uptime_ms=%lu heap=%lu force_stack_free=%lu\n", (unsigned long)millis(),
            (unsigned long)ESP.getFreeHeap(),
            (unsigned long)(forceCalculationTaskHandle
                                ? uxTaskGetStackHighWaterMark(forceCalculationTaskHandle)
                                : 0));
    }
}
#endif

static void initialize_motor_transport()
{
#if MOTOR_TRANSPORT == 1
    const bool canStarted =
        canHal.begin(CAN_TX_PIN, CAN_RX_PIN, CAN_BITRATE, CAN_POSITION_ID, CAN_SINGLE_SHOT);
    if (!canStarted)
        SunFFB::diagnostic_printf("CAN initialization failed\n");
    configASSERT(canStarted);
#else
    comSerial.begin(UART1_BAUD, SERIAL_8N1, RDX_PIN, TDX_PIN);

    // Deliver each short frame promptly instead of waiting for a large FIFO batch.
    comSerial.setRxFIFOFull(sizeof(SunFFB::PositionPayload) + (SERIAL_FRAMING_MODE == 1 ? 3 : 4));
    comSerial.setRxTimeout(1);

#endif
}

static void initialize_joystick()
{
    // ADC calibration treats the boot position as center. Hold the controls at
    // their intended neutral position during these samples. External encoders
    // use their protocol center instead and skip ADC setup/calibration entirely.
#ifdef USE_BUTTON
    pinMode(SW_PIN, INPUT);
#endif
#if !USE_EXTERNAL_POSITION
    analogReadResolution(12); // ADC_CLAMP/ADC_SCALE assume 12-bit samples on S2 and S3.
#pragma unroll
    for (uint8_t i = 0; i < NUM_AXIS; ++i)
    {
        pinMode(joystickPins[i], INPUT);
        uint32_t o = 0;

        for (auto j = 0; j < ADC_CALIBRATION_SAMPLES; ++j)
        {
            o += analogRead(joystickPins[i]);
            delay(1);
        }

        coordOffsets[i] = o / ADC_CALIBRATION_SAMPLES;
    }
#endif
}

#if ENABLE_LCD
static void initialize_lcd()
{
    // Create the long-lived framebuffer before starting display/control tasks.
    // The splash is a synchronous startup transfer; later frames use row groups.
    lcd.init();
    lcd.setRotation(1);
    lcd.setTextWrap(true, true);
    void* spriteBuffer = sprite.createSprite(TFT_W, TFT_H);
    configASSERT(spriteBuffer != nullptr);
    sprite.setTextWrap(true, true);
    sprite.fillSprite(TFT_BLACK);
    sprite.setCursor(0, 0);
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextFont(1);
    sprite.printf("SunFFB Joystick");
    sprite.pushSprite(TFT_X, TFT_Y);
}
#endif

static void initialize_model_and_queues()
{
    ffbHandler.init();
    // Initialize the protocol model before publishing USB callbacks. Queues,
    // mutexes and the input model must also exist before USB.begin() can expose
    // the device to host requests, even though worker tasks are not yet started.

    gPositions = xQueueCreate(1, sizeof(PositionSample));
    // Single-slot overwrite queues are latest-value mailboxes, not full sample-history buffers.
    gForces = xQueueCreate(1, sizeof(int32_t) * NUM_AXIS);
    gJoystickReportData = xQueueCreate(1, sizeof(JoystickSample));
    forceStartGate = xSemaphoreCreateBinary();
    semaphoreFFBDeviceInput = xSemaphoreCreateMutex();
    semaphoreFFBReportHandler = xSemaphoreCreateMutex();
    configASSERT(gPositions && gForces && gJoystickReportData && forceStartGate &&
                 semaphoreFFBDeviceInput && semaphoreFFBReportHandler);
    ffbDeviceInput.reset();
}

static void initialize_usb()
{
    USB.VID(DEVICE_VID);
    USB.PID(DEVICE_PID);
    USB.manufacturerName(DEVICE_MANUFACTURER);
    USB.productName(DEVICE_PRODUCT);

    const bool hidDeviceAdded = USBHID::addDevice(&ffbUsbHidDevice, sizeof(ffbReportDescriptor));
    configASSERT(hidDeviceAdded);
    usb_hid.begin();
    // HID registration is complete; the native USB device exposes HID only.
    const bool usbStarted = USB.begin();
    configASSERT(usbStarted);
}

void setup()
{
    // Preserve startup order: callbacks can run as soon as USB.begin() returns,
    // and each created task can preempt setup before the next task is created.
    const BaseType_t appCore = applicationCore;
    constexpr BaseType_t protoCore = 0;

#if ENABLE_TIMING_DIAGNOSTICS || MOTOR_TRANSPORT == 1
    // Avoid allocating the console queue/task when this build has no log producers.
    SunFFB::start_diagnostic_console(Serial, SERIAL_BAUD, TASK_STACK_SIZE, protoCore);
#endif
    initialize_motor_transport();
    initialize_joystick();
#if ENABLE_LCD
    initialize_lcd();
#endif
    initialize_model_and_queues();
    initialize_usb();

    // Start processing even before a host enumerates the USB interface. Tasks
    // can run immediately when created, so all shared resources are ready above.
    // The force task alone waits on the gate until the final peer is created.

#if ENABLE_TIMING_DIAGNOSTICS
    createFirmwareTask(timing_task, "Timing", TIMING_TASK_STACK_SIZE, 1, protoCore);
#endif
#if ENABLE_LCD
    createFirmwareTask(lcd_task, "LCD", LCD_TASK_STACK_SIZE, 1, appCore);
#endif
    createFirmwareTask(joystick_task, "Joystick", TASK_STACK_SIZE, 2, appCore);
    createFirmwareTask(force_calculation_task, "Force", TASK_STACK_SIZE, 3, appCore);

    createFirmwareTask(send_report_task, "Report", TASK_STACK_SIZE, periodicIOPriority, protoCore);
#if ENABLE_MOTOR_OUTPUT
    createFirmwareTask(send_force_task, "SendForce", TASK_STACK_SIZE, periodicIOPriority,
                       protoCore);
#endif
#if USE_EXTERNAL_POSITION || MOTOR_TRANSPORT == 1
    createFirmwareTask(receive_position_task, "ReceivePos", TASK_STACK_SIZE, periodicIOPriority,
                       protoCore);
#endif
    // The force task may now establish its periodic release phase.
    xSemaphoreGive(forceStartGate);
}

void loop()
{
    // Delete the Arduino loop task and release its stack after setup; worker tasks continue independently.
    vTaskDelete(nullptr);
}
