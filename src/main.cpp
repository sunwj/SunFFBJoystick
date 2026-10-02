#include <Arduino.h>
#include <FreeRTOS.h>
#include <Adafruit_TinyUSB.h>
#include <memory.h>
#include <algorithm>
#include "constants.h"
#if ENABLE_LCD
#include <TFT_eSPI.h>
#endif
#include "ffb_report_types.h"
#include "ffb_report_descriptor.h"
#include "ffb_report_handler.h"
#include "ffb_report_dispatch.h"
#include "ffb_device_input.h"
#include "ffb_force_calculator.h"
#include "communication/serial_hal_arduino.h"
#include "communication/serial_link.h"
#include "realtime_timing.h"
// The 1 ms force loop requires the ESP Arduino core's 1 kHz RTOS tick.
static_assert(configTICK_RATE_HZ == 1000, "Force scheduling requires a 1 ms RTOS tick");
static_assert(FORCE_TASK_PERIOD_MS == 1 || FORCE_TASK_PERIOD_MS == 2, "Force rate must be 500 or 1000 Hz");
// 8N1: ten wire bits per byte, four framing bytes. TX/RX are full duplex.
static_assert((4 + sizeof(SunFFB::ForcePayload)) * 10 * 500 < UART1_BAUD,
              "Motor UART cannot carry 500 Hz force frames");
#if CONFIG_FREERTOS_UNICORE
constexpr BaseType_t applicationCore = 0;
// Keep periodic I/O below force calculation on a shared CPU.
constexpr UBaseType_t periodicIOPriority = 2;
#else
constexpr BaseType_t applicationCore = 1;
constexpr UBaseType_t periodicIOPriority = configMAX_PRIORITIES - 1;
#endif

static void createFirmwareTask(TaskFunction_t entry, const char* name,
                               uint32_t stackBytes, UBaseType_t priority,
                               BaseType_t core)
{
    const BaseType_t result = xTaskCreatePinnedToCore(
        entry, name, stackBytes, nullptr, priority, nullptr, core);
    configASSERT(result == pdPASS);
}

#if NUM_AXIS == 1
constexpr uint8_t joystickPins[NUM_AXIS] = {AXIS0_ADC_PIN};
#elif NUM_AXIS == 2
constexpr uint8_t joystickPins[NUM_AXIS] = {AXIS0_ADC_PIN, AXIS1_ADC_PIN};
#elif NUM_AXIS == 3
constexpr uint8_t joystickPins[NUM_AXIS] = {AXIS0_ADC_PIN, AXIS1_ADC_PIN, AXIS2_ADC_PIN};
#endif

#if !USE_SERIAL_POSITION
static_assert(AXIS0_ADC_PIN != 19 && AXIS0_ADC_PIN != 20, "ADC pin conflicts with USB");
#if NUM_AXIS >= 2
static_assert(AXIS1_ADC_PIN != 19 && AXIS1_ADC_PIN != 20, "ADC pin conflicts with USB");
#endif
#if NUM_AXIS == 3
static_assert(AXIS2_ADC_PIN != 19 && AXIS2_ADC_PIN != 20, "ADC pin conflicts with USB");
#endif
#endif

uint16_t coordOffsets[NUM_AXIS] = {0};

struct PositionSample {
    uint16_t position[NUM_AXIS];
    uint32_t receivedUs;
    uint32_t sequence;
};
struct JoystickSample {
    SunFFB::JoystickInputReportData report;
    uint32_t receivedUs;
    uint32_t sequence;
};
SunFFB::TimingStream forceTiming(FORCE_TASK_PERIOD_MS * 1000);
SunFFB::TimingStream positionRxTiming(2000), inputTiming(2000);
SunFFB::TimingStream usbSubmitTiming(2000), usbCompleteTiming(2000);
SunFFB::TimingStream freshPositionTiming(2000), motorTxTiming(2000);
// TinyUSB calls this after the interrupt IN transfer has actually completed.
extern "C" void tud_hid_report_complete_cb(uint8_t instance, const uint8_t* report, uint16_t len)
{
    if (instance == 0 && len && report[0] == REPORT_ID_JOYSTICK)
        usbCompleteTiming.record(micros());
}

volatile uint32_t reportProcessTime = 0;
volatile uint32_t effectProcessTime = 0;

QueueHandle_t gPositions;
QueueHandle_t gForces;
QueueHandle_t gJoystickReportData;

HardwareSerial comSerial(1);
SunFFB::ArduinoSerialHal serialHal(comSerial);
SunFFB::FFBSerialLink<SunFFB::ArduinoSerialHal> serialLink(serialHal);

#if ENABLE_LCD
TFT_eSPI lcd = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite( & lcd);
#endif

Adafruit_USBD_HID usb_hid;
SunFFB::FFBDeviceInput ffbDeviceInput;
SunFFB::FFBReportHandler ffbHandler;
SunFFB::FFBForceCalculator ffbForceCalculator;

TaskHandle_t forceCalculationTaskHandle;
SemaphoreHandle_t semaphoreFFBDeviceInput;
SemaphoreHandle_t semaphoreFFBReportHandler;

static const uint8_t ffbReportDescriptor[] PROGMEM = {
    FFB_REPORT_DESCRIPTOR_CONTENT
};

uint16_t hid_get_report_callback(uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    switch (report_type)
    {
        case HID_REPORT_TYPE_FEATURE:
            switch (report_id)
            {
                // Block Load Request
                case REPORT_ID_BLOCK_LOAD_REPORT:
                {
                    if (!buffer || reqlen < 4) return 0;
                    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
                    const SunFFB::BlockLoadReportData* data = ffbHandler.get_block_load_report_data();
                    memcpy(buffer, data, sizeof(SunFFB::BlockLoadReportData));
                    xSemaphoreGive(semaphoreFFBReportHandler);

                    #ifdef SERIAL_PRINT
                    Serial.printf("Block load: idx=%d status=%d (1=success, 2=full)\n", data->effectBlockIndex, data->blockLoadStatus);
                    #endif
                    return 4;
                }

                // Pool size, max simultaneous effects, etc.
                case REPORT_ID_POOL_REPORT:
                {
                    if (!buffer || reqlen < 4) return 0;
                    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
                    const SunFFB::PoolReportData* data = ffbHandler.get_pool_report_data();
                    memcpy(buffer, data, sizeof(SunFFB::PoolReportData));
                    xSemaphoreGive(semaphoreFFBReportHandler);

                    #ifdef SERIAL_PRINT
                    Serial.printf("Pool report.\n");
                    #endif
                    return 4;
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

// A single TinyUSB task produces commands. Completion preserves the ordering
// of SET_REPORT and subsequent GET_REPORT without sharing USB buffers.
struct HIDCommand {
    uint8_t id;
    uint8_t type;
    uint16_t length;
    uint8_t data[sizeof(SunFFB::SetEffectReportData)];
};
static_assert(sizeof(SunFFB::SetEffectReportData) >= sizeof(SunFFB::SetConditionReportData));
QueueHandle_t gHIDCommands;
SemaphoreHandle_t semaphoreHIDCommandDone;

void hid_command_task(void*)
{
    HIDCommand command;
    while (true) {
        xQueueReceive(gHIDCommands, &command, portMAX_DELAY);
        xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
        SunFFB::dispatch_output_report(ffbHandler, command.id, command.type,
                                      command.data, command.length);
        xSemaphoreGive(semaphoreFFBReportHandler);
        xSemaphoreGive(semaphoreHIDCommandDone);
    }
}

void hid_set_report_callback(uint8_t reportId, hid_report_type_t reportType,
                             const uint8_t* buffer, uint16_t bufSize)
{
    const uint32_t startTime = micros();
    if (!SunFFB::valid_output_report(reportId, reportType, buffer, bufSize)) return;
    HIDCommand command{};
    command.id = reportId;
    command.type = reportType;
    command.length = bufSize;
    memcpy(command.data, buffer, bufSize);
    xQueueSend(gHIDCommands, &command, portMAX_DELAY);
    xSemaphoreTake(semaphoreHIDCommandDone, portMAX_DELAY);
    reportProcessTime = micros() - startTime;
}

#if ENABLE_LCD
void lcd_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        int32_t forces[NUM_AXIS] = {0};
        xQueuePeek(gForces, forces, 0);

        sprite.fillSprite(TFT_BLACK);
        sprite.drawRect(0, 0, 40, 40, TFT_RED);

        // snapshot axis data under semaphore for consistent reads
        static int16_t axisSnapshot[NUM_AXIS] = {0};
        if (xSemaphoreTake(semaphoreFFBDeviceInput, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            memcpy(axisSnapshot, (const int16_t*)ffbDeviceInput.inputData.axis, sizeof(axisSnapshot));
            xSemaphoreGive(semaphoreFFBDeviceInput);
        }

        uint8_t coords[NUM_AXIS];
        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
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

        sprite.setCursor(40, 0);
        sprite.printf("Report time: %d us", reportProcessTime);
        sprite.setCursor(40, 11);
        if (effectProcessTime > FORCE_TASK_MAX_TIME_US)
        {
            sprite.setTextColor(TFT_RED);
            sprite.printf("Effect time: %d us !", effectProcessTime);
        }
        else
        {
            sprite.setTextColor(TFT_WHITE);
            sprite.printf("Effect time: %d us", effectProcessTime);
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
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            const float center = i == 1 ? 60.f : 20.f;
            coords[i] = std::clamp(forces[i] / float(USB_MAX_MAGNITUDE) * 20 + center, center - 18.f, center + 18.f);
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

    sprite.pushSprite(TFT_X, TFT_Y);
    
    vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(LCD_TASK_PERIOD_MS));
    }
}

#endif // ENABLE_LCD

void joystick_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    uint32_t sequence = 0;
    while (true)
    {
        uint32_t receivedUs;
        int16_t coords[NUM_AXIS];
#if USE_SERIAL_POSITION
        PositionSample sample;
        xQueueReceive(gPositions, &sample, portMAX_DELAY);
        receivedUs = sample.receivedUs;
        sequence = sample.sequence;
        for (uint8_t i = 0; i < NUM_AXIS; ++i)
            coords[i] = std::clamp(int32_t(sample.position[i]) - 32768, -32767, 32767);
#else
        receivedUs = micros();
        ++sequence;
        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            coords[i] = analogRead(joystickPins[i]) - coordOffsets[i];
            coords[i] = std::clamp(coords[i], int16_t(-ADC_CLAMP), int16_t(ADC_CLAMP));
            coords[i] = coords[i] / ADC_SCALE * USB_AXIS_MAX_ABSOLUTE;
        }

#endif
#ifdef USE_BUTTON
        uint8_t btnState = digitalRead(SW_PIN) ? 0 : 1;
#endif

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
#if !USE_SERIAL_POSITION
        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(JOYSTICK_TASK_PERIOD_MS));
#endif
    }
}

void force_calculation_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        uint32_t startTime = micros();
        int32_t forces[NUM_AXIS] = {0};

        xSemaphoreTake(semaphoreFFBDeviceInput, portMAX_DELAY);
        xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
        ffbForceCalculator.force_calculator(ffbHandler, ffbDeviceInput, (int32_t*)forces);
        xSemaphoreGive(semaphoreFFBReportHandler);
        xSemaphoreGive(semaphoreFFBDeviceInput);

        xQueueOverwrite(gForces, forces);

        uint32_t endTime = micros();
        effectProcessTime = endTime - startTime;
        forceTiming.record(endTime, effectProcessTime);

        // A missed deadline must not turn the highest periodic task into a
        // busy loop, especially when all firmware tasks share one CPU.
        if (xTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(FORCE_TASK_PERIOD_MS)) == pdFALSE) {
            vTaskDelay(1);
            wakeupTime = xTaskGetTickCount();
        }
    }
}

void send_report_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    SunFFB::ReportSchedule schedule(POSITION_REPORT_PERIOD_MS * 1000, micros());
    uint32_t lastSequence = 0;
    bool hasSequence = false;
    while (true)
    {
        const uint32_t nowUs = micros();
        if (usb_hid.ready()) {
            JoystickSample sample;
            const bool hasSample = xQueuePeek(gJoystickReportData, &sample, 0) == pdTRUE;
            const bool fresh = hasSample && (!hasSequence || sample.sequence != lastSequence);
#if USE_SERIAL_POSITION
            // Forward arrivals, not a second free-running 500 Hz sampler.
            // No duplicate reports can masquerade as fresh motor positions.
            const bool positionDue = fresh;
#else
            const bool positionDue = schedule.due(nowUs);
#endif
            if (positionDue) {
                if (hasSample) {
                    if (usb_hid.sendReport(REPORT_ID_JOYSTICK, &sample.report, sizeof(sample.report))) {
                        schedule.sent(nowUs);
                        const uint32_t submittedUs = micros();
                        usbSubmitTiming.record(submittedUs, submittedUs - sample.receivedUs);
                        if (fresh) {
                            freshPositionTiming.record(micros());
                            lastSequence = sample.sequence;
                            hasSequence = true;
                        }
                    } else usbSubmitTiming.failed();
                }
            } else if (xSemaphoreTake(semaphoreFFBReportHandler, 0) == pdTRUE) {
                // State uses spare 1 ms endpoint slots; new position always has priority.
                if (ffbHandler.pidStateDirty) {
                    const auto data = *ffbHandler.get_pid_state_report_data();
                    if (usb_hid.sendReport(REPORT_ID_PID_STATE, &data, sizeof(data)))
                        ffbHandler.pidStateDirty = false;
                }
                xSemaphoreGive(semaphoreFFBReportHandler);
            }
        }
        if (xTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(1)) == pdFALSE) {
            vTaskDelay(1);
            wakeupTime = xTaskGetTickCount();
        }
    }
}

void send_force_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        int32_t forces[NUM_AXIS];
        xQueuePeek(gForces, forces, portMAX_DELAY);
        const uint32_t startUs = micros();
        if (serialLink.sendForce(forces)) motorTxTiming.record(micros(), micros() - startUs);
        else motorTxTiming.failed();

        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(SEND_FORCE_TASK_PERIOD_MS));
    }
}

void receive_position_task(void* params)
{
    uint32_t sequence = 0;
    while (true)
    {
        // Eight bounded parser calls per 1 ms; drain heartbeat/noise as well.
        for (uint8_t frame = 0; frame < 8 && serialHal.available(); ++frame) {
            SunFFB::PositionPayload payload;
            if (!serialLink.receivePosition(payload)) continue;
            PositionSample sample{};
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
    SunFFB::TimingStream* streams[] = {&forceTiming, &positionRxTiming, &inputTiming,
        &usbSubmitTiming, &usbCompleteTiming, &freshPositionTiming, &motorTxTiming};
    const char* names[] = {"force", "rx", "input", "usb_submit", "usb_done", "fresh", "motor_tx"};
    uint32_t previous[7]{};
    uint32_t lastUs = micros();
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        const uint32_t nowUs = micros();
        const uint32_t windowUs = nowUs - lastUs;
        lastUs = nowUs;
        for (uint8_t i = 0; i < 7; ++i) {
            const auto stat = streams[i]->snapshot();
            Serial.printf("TIMING %s hz=%.1f max_gap_us=%lu gap_over_budget=%lu max_work_us=%lu fail=%lu\n",
                names[i], float(stat.count - previous[i]) * 1000000.f / windowUs,
                (unsigned long)stat.maxGapUs, (unsigned long)stat.overBudget,
                (unsigned long)stat.maxWorkUs, (unsigned long)stat.failures);
            previous[i] = stat.count;
        }
        Serial.printf("TIMING max_position_age_at_submit_us=%lu\n",
            (unsigned long)usbSubmitTiming.snapshot().maxWorkUs);
    }
}
#endif

void setup()
{
    const BaseType_t appCore = applicationCore;
    uint8_t protoCore = 0;

    Serial.begin(SERIAL_BAUD);
    comSerial.begin(UART1_BAUD, SERIAL_8N1, RDX_PIN, TDX_PIN);
    // Deliver each short frame promptly instead of waiting for a large FIFO batch.
    comSerial.setRxFIFOFull(sizeof(SunFFB::PositionPayload) + 4);
    comSerial.setRxTimeout(1);

    // init joystick and calibrate
#ifdef USE_BUTTON
    pinMode(SW_PIN, INPUT);
#endif
#if !USE_SERIAL_POSITION
    analogReadResolution(12); // ADC_CLAMP/ADC_SCALE assume 12-bit samples on S2 and S3.
    #pragma unroll
    for(uint8_t i = 0; i < NUM_AXIS; ++i)
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


#if ENABLE_LCD
    lcd.init();
    lcd.setRotation(1);
    lcd.setTextWrap(true, true);
    configASSERT(sprite.createSprite(TFT_W, TFT_H) != nullptr);
    sprite.setTextWrap(true, true);
    sprite.fillSprite(TFT_BLACK);
    sprite.setCursor(0, 0);
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextFont(1);
    sprite.printf("SunFFB Joystick");
    sprite.pushSprite(TFT_X, TFT_Y);
#endif

    ffbHandler.init();

    gPositions = xQueueCreate(1, sizeof(PositionSample));
    gForces = xQueueCreate(1, sizeof(int32_t) * NUM_AXIS);
    gJoystickReportData = xQueueCreate(1, sizeof(JoystickSample));
    gHIDCommands = xQueueCreate(8, sizeof(HIDCommand));
    semaphoreFFBDeviceInput = xSemaphoreCreateMutex();
    semaphoreFFBReportHandler = xSemaphoreCreateMutex();
    semaphoreHIDCommandDone = xSemaphoreCreateBinary();
    configASSERT(gPositions && gForces && gJoystickReportData && gHIDCommands &&
                 semaphoreFFBDeviceInput && semaphoreFFBReportHandler && semaphoreHIDCommandDone);
    ffbDeviceInput.reset();
    const BaseType_t commandTaskCreated = xTaskCreatePinnedToCore(
        hid_command_task, "HIDCommand", TASK_STACK_SIZE, nullptr,
        configMAX_PRIORITIES - 1, nullptr, protoCore);
    configASSERT(commandTaskCreated == pdPASS);

    // Manual begin() is required on core without built-in support e.g. mbed rp2040
    TinyUSBDevice.setID(USB_VID, USB_PID);
    TinyUSBDevice.setManufacturerDescriptor(USB_MANUFACTURER);
    TinyUSBDevice.setProductDescriptor(USB_PRODUCT);
    if (!TinyUSBDevice.isInitialized())
        TinyUSBDevice.begin(0);

    // Setup HID
    usb_hid.setBootProtocol(HID_ITF_PROTOCOL_NONE);
    usb_hid.setPollInterval(POLLING_RATE);
    usb_hid.setReportDescriptor(ffbReportDescriptor, sizeof(ffbReportDescriptor));
    usb_hid.setReportCallback(hid_get_report_callback, hid_set_report_callback);

    usb_hid.begin();

    // If already enumerated, additional class driverr begin() e.g msc, hid, midi won't take effect until re-enumeration
    if (TinyUSBDevice.mounted())
    {
        TinyUSBDevice.detach();
        delay(10);
        TinyUSBDevice.attach();
    }

    // Start processing even before a host enumerates the USB interface.

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
    createFirmwareTask(send_force_task, "SendForce", TASK_STACK_SIZE, periodicIOPriority, protoCore);
#endif
#if USE_SERIAL_POSITION
    createFirmwareTask(receive_position_task, "ReceivePos", TASK_STACK_SIZE, periodicIOPriority, protoCore);
#endif
}

void loop()
{
    vTaskDelete(nullptr);
}
