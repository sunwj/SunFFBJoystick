#include <Arduino.h>
#include <FreeRTOS.h>
#include <memory.h>
#include <algorithm>
#include <TFT_eSPI.h>
#include "tusb.h"
#include "esp32-hal-tinyusb.h"
#include "constants.h"
#include "ffb_report_types.h"
#include "usb/ffb_report_descriptor.h"
#include "ffb_report_handler.h"
#include "ffb_device_input.h"
#include "ffb_force_calculator.h"
#include "simple_serial_communication.h"

// #define SERIAL_PRINT

#define TFT_X 0
#define TFT_Y 0
#define TFT_W 160
#define TFT_H 80

#define SW_PIN 16
#define TDX_PIN 4
#define RDX_PIN 5

#if NUM_AXIS == 1
constexpr uint8_t joystickPins[NUM_AXIS] = {18};
#elif NUM_AXIS == 2
constexpr uint8_t joystickPins[NUM_AXIS] = {18, 17};
#elif NUM_AXIS == 3
constexpr uint8_t joystickPins[NUM_AXIS] = {18, 17, 19};
#endif

uint16_t coordOffsets[NUM_AXIS] = {0};

volatile uint32_t reportProcessTime = 0;
volatile uint32_t effectProcessTime = 0;

QueueHandle_t gPositions;
QueueHandle_t gForces;

HardwareSerial comSerial(1);

TFT_eSPI lcd = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite( & lcd);

SunFFB::FFBDeviceInput ffbDeviceInput;
SunFFB::FFBReportHandler ffbHandler;
SunFFB::FFBForceCalculator ffbForceCalculator;

TaskHandle_t forceCalculationTaskHandle;
SemaphoreHandle_t semaphoreFFBDeviceInput;
SemaphoreHandle_t semaphoreFFBReportHandler;

static SemaphoreHandle_t tinyusb_hid_device_input_sem = NULL;
static SemaphoreHandle_t tinyusb_hid_device_input_mutex = NULL;

static uint16_t timeout_ms = 100;

extern "C" uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
    (void) instance;
    (void) reqlen;

    switch (report_id)
    {
        case REPORT_ID_BLOCK_LOAD_REPORT:
        {
            if (report_type != HID_REPORT_TYPE_FEATURE) return 0;
            if (reqlen < sizeof(SunFFB::BlockLoadReportData)) return 0;

            memcpy(buffer, (const void*)ffbHandler.get_block_load_report_data(), sizeof(SunFFB::BlockLoadReportData));
            return sizeof(SunFFB::BlockLoadReportData);
        }

        case REPORT_ID_POOL_REPORT:
        {
            if (report_type != HID_REPORT_TYPE_FEATURE) return 0;
            if (reqlen < sizeof(SunFFB::PoolReportData)) return 0;

            memcpy(buffer, (const void*)ffbHandler.get_pool_report_data(), sizeof(SunFFB::PoolReportData));
            return sizeof(SunFFB::PoolReportData);
        }

        default:
            return 0; // STALL unsupported GET_REPORT
    }
}

extern "C"  void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
    (void) instance;

    uint32_t startTime = micros();

    if (buffer == nullptr || bufsize == 0) return;

    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
    switch (report_id)
    {
        case REPORT_ID_SET_EFFECT_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::SetEffectReportData))
            {
                ffbHandler.set_effect((const SunFFB::SetEffectReportData*)buffer);
            }
            break;

        case REPORT_ID_SET_ENVELOPE_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::SetEnvelopeReportData))
            {
                ffbHandler.set_envelope((const SunFFB::SetEnvelopeReportData*)buffer);
            }
            break;

        case REPORT_ID_SET_CONDITION_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::SetConditionReportData))
            {
                ffbHandler.set_condition((const SunFFB::SetConditionReportData*)buffer);
            }
            break;

        case REPORT_ID_SET_PERIODIC_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::SetPeriodicReportData))
            {
                ffbHandler.set_periodic((const SunFFB::SetPeriodicReportData*)buffer);
            }
            break;

        case REPORT_ID_SET_CONSTANT_FORCE_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::SetConstantForceReportData))
            {
                ffbHandler.set_constant_force((const SunFFB::SetConstantForceReportData*)buffer);
            }
            break;

        case REPORT_ID_SET_RAMP_FORCE_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::SetRampForceReportData))
            {
                ffbHandler.set_ramp_force((const SunFFB::SetRampForceReportData*)buffer);
            }
            break;

        case REPORT_ID_EFFECT_OPERATION_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::EffectOperationReportData))
            {
                ffbHandler.set_effect_operation((const SunFFB::EffectOperationReportData*)buffer);
            }
            break;

        case REPORT_ID_BLOCK_FREE_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::BlockFreeReportData))
            {
                ffbHandler.set_effect_block_free((SunFFB::BlockFreeReportData*)buffer);
            }
            break;

        case REPORT_ID_DEVICE_CONTROL_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::DeviceControlReportData))
            {
                ffbHandler.set_device_control((const SunFFB::DeviceControlReportData*)buffer);
                // tud_hid_report(REPORT_ID_PID_STATE, (void*)ffbHandler.get_pid_state_report_data(), sizeof(SunFFB::PIDStateReportData));
            }
            break;

        case REPORT_ID_DEVICE_GAIN_REPORT:
            if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= sizeof(SunFFB::DeviceGainReportData))
            {
                ffbHandler.set_device_gain((const SunFFB::DeviceGainReportData*)buffer);
            }
            break;
        
        case REPORT_ID_CREATE_NEW_EFFECT_REPORT:
            if (report_type == HID_REPORT_TYPE_FEATURE && bufsize >= sizeof(SunFFB::CreateNewEffectReportData))
            {
                ffbHandler.create_new_effect((const SunFFB::CreateNewEffectReportData*)buffer);
            }
            break;

        default:
            break;
    }
    xSemaphoreGive(semaphoreFFBReportHandler);

    uint32_t endTime = micros();
    if (startTime < endTime)
        reportProcessTime = (endTime - startTime);
}

extern "C" void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    if (tinyusb_hid_device_input_sem)
        xSemaphoreGive(tinyusb_hid_device_input_sem);
}

void lcd_task(void* params)
{
    uint32_t cnt = 0;
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        int32_t forces[NUM_AXIS] = {0};
        xQueuePeek(gForces, forces, 0);

        sprite.fillSprite(TFT_BLACK);
        sprite.drawRect(0, 0, 40, 40, TFT_RED);
        uint8_t coords[NUM_AXIS];
        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            uint8_t c = ffbDeviceInput.inputData.axis[i] / float(USB_AXIS_MAX_ABSOLUTE) * 20 + 20;
            c = c < 2 ? 2 : c;
            c = c > 38 ? 38 : c;
            coords[i] = c;
        }
        #if NUM_AXIS == 1
        sprite.fillSmoothCircle(coords[0], 20, 2, TFT_GREEN, TFT_BLACK);
        #elif NUM_AXIS == 2
        sprite.fillSmoothCircle(coords[0], coords[1], 2, TFT_GREEN, TFT_BLACK);
        #endif

        sprite.setCursor(40, 0);
        sprite.printf("Report time: %d us", reportProcessTime);
        sprite.setCursor(40, 11);
        sprite.printf("Effect time: %d us", effectProcessTime);
        #if NUM_AXIS == 1
        sprite.setCursor(40, 23);
        sprite.printf("AX: %d", ffbDeviceInput.inputData.axis[0]);
        #elif NUM_AXIS == 2
        sprite.setCursor(40, 23);
        sprite.printf("AX: %d", ffbDeviceInput.inputData.axis[0]);
        sprite.setCursor(100, 23);
        sprite.printf("AY: %d", ffbDeviceInput.inputData.axis[1]);
        #endif

        constexpr uint8_t yOffsets[2] = {20, 60};
        constexpr uint8_t yLimitsOffsets[2] = {0, 40};
        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            uint8_t c = forces[i] / float(USB_MAX_MAGNITUDE) * 20 + yOffsets[i];
            c = c < (2 + yLimitsOffsets[i]) ? (2 + yLimitsOffsets[i]) : c;
            c = c > (38 + yLimitsOffsets[i]) ? (38 + yLimitsOffsets[i]) : c;
            coords[i] = c;
        }
        sprite.drawRect(0, 40, 40, 40, TFT_RED);
        #if NUM_AXIS == 1
        sprite.fillSmoothCircle(coords[0], 60, 2, TFT_GREEN, TFT_BLACK);
        sprite.drawWideLine(20, 60, coords[0], 60, 2, TFT_GREEN, TFT_BLACK);
        #elif NUM_AXIS == 2
        sprite.fillSmoothCircle(coords[0], coords[1], 2, TFT_GREEN, TFT_BLACK);
        sprite.drawWideLine(20, 60, coords[0], coords[1], 2, TFT_GREEN, TFT_BLACK);
        #endif

        #if NUM_AXIS == 1
        sprite.setCursor(40, 35);
        sprite.printf("FX: %d", forces[0]);
        #elif NUM_AXIS == 2
        sprite.setCursor(40, 35);
        sprite.printf("FX: %d", forces[0]);
        sprite.setCursor(100, 35);
        sprite.printf("FY: %d", forces[1]);
        #endif

        uint32_t currentTime = millis() * 1e-3;
        sprite.setCursor(124, 70);
        sprite.printf("%d", currentTime);

        const SunFFB::EffectBlock * blocks = ffbHandler.get_all_effect_blocks();
        for (uint8_t i = 0; i < MAX_EFFECTS; ++i)
        {
            uint16_t color = TFT_BLACK;
            if (EFFECT_STATE_FREE == blocks[i].state)
                color = TFT_GREEN;
            else if (blocks[i].state & EFFECT_STATE_ALLOCATED)
            {
                color = TFT_BLUE;
                if (blocks[i].state & EFFECT_STATE_PLAYING)
                    color = TFT_RED;
            }
            sprite.fillSmoothCircle(44 + i % 14 * 8, 48 + 8 * int(i / 14), 4, color, TFT_BLACK);
        }

        sprite.pushSprite(0, 0);
        vTaskDelayUntil( & wakeupTime, pdMS_TO_TICKS(60));
    }
}

uint32_t count = 0;

void joystick_task(void* params)
{
    ffbDeviceInput.reset();
    ffbDeviceInput.set_tf_speed(0.1f);
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        int16_t coords[NUM_AXIS];
        #pragma unroll
        for(uint8_t i = 0; i < NUM_AXIS; ++i)
        {
            coords[i] = analogRead(joystickPins[i]) - coordOffsets[i];
            coords[i] = std::clamp(coords[i], int16_t(-2048), int16_t(2048));
            coords[i] = coords[i] / 2048.f * USB_AXIS_MAX_ABSOLUTE;
        }

        xSemaphoreTake(semaphoreFFBDeviceInput, portMAX_DELAY);
        ffbDeviceInput.update_axis(coords);
        xSemaphoreGive(semaphoreFFBDeviceInput);

        xSemaphoreTake(tinyusb_hid_device_input_mutex, pdMS_TO_TICKS(timeout_ms));
        if (tud_hid_ready())
        {
            xSemaphoreTake(tinyusb_hid_device_input_sem, 0);
            if (tud_hid_n_report(0, REPORT_ID_JOYSTICK, (void*)&ffbDeviceInput.inputData, sizeof(SunFFB::JoystickInputReportData)))
            {
                xSemaphoreTake(tinyusb_hid_device_input_sem, pdMS_TO_TICKS(timeout_ms));
            }
        }
        xSemaphoreGive(tinyusb_hid_device_input_mutex);

        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(2));
    }

    // ffbDeviceInput.reset();
    // // int32_t speedDeadBand[NUM_AXIS] = {30, 30};
    // // ffbDeviceInput.update_speed_deadband(speedDeadBand);
    // ffbDeviceInput.set_tf_speed(0.025f);
    
    // TickType_t wakeupTime = xTaskGetTickCount();
    // while (true)
    // {
    //     int16_t coords[NUM_AXIS] = {0};
    //     xQueuePeek(gPositions, coords, portMAX_DELAY);

    //     xSemaphoreTake(semaphoreFFBDeviceInput, portMAX_DELAY);
    //     ffbDeviceInput.update_axis(coords);
    //     xSemaphoreGive(semaphoreFFBDeviceInput);

    //     usb_hid.sendReport(REPORT_ID_JOYSTICK, (void*)&ffbDeviceInput.inputData, sizeof(SunFFB::JoystickInputReportData));

    //     vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(2));
    // }
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

        // const int32_t* pos = ffbDeviceInput.get_position();
        // forces[0] = pos[0] >= 32766 ? -10000 : forces[0];
        // forces[1] = pos[1] >= 32766 ? -10000 : forces[1];
        // forces[0] = pos[0] <= -32766 ? 10000 : forces[0];
        // forces[1] = pos[1] <= -32766 ? 10000 : forces[1];

        xQueueOverwrite(gForces, forces);

        uint32_t endTime = micros();
        if (startTime < endTime)
            effectProcessTime = (endTime - startTime);

        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(2));
    }
}

void send_force_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        int32_t forces[NUM_AXIS];
        xQueuePeek(gForces, forces, portMAX_DELAY);
        send_packet_buffer(comSerial, (uint8_t*)forces, sizeof(forces));

        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(2));
    }
}

void receive_position_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        uint16_t pos[NUM_AXIS];
        if(receive_packet_buffer(comSerial, (uint8_t*)pos, sizeof(pos)))
        {
            xQueueOverwrite(gPositions, pos);
            continue;
        }

        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(2));
    }
}

void setup()
{
    uint8_t appCore = xTaskGetAffinity(nullptr);
    uint8_t protoCore = 0;

    Serial.begin(115200);
    comSerial.begin(115200, SERIAL_8N1, RDX_PIN, TDX_PIN);

    // init joystick and calibrate
    pinMode(SW_PIN, INPUT);
    #pragma unroll
    for(uint8_t i = 0; i < NUM_AXIS; ++i)
    {
        pinMode(joystickPins[i], INPUT);
        uint32_t o = 0;
        for (auto j = 0; j < 1000; ++j)
        {
            o += analogRead(joystickPins[i]);
            delay(1);
        }
        coordOffsets[i] = o / 1000;
    }

    lcd.init();
    lcd.setRotation(1);
    lcd.setTextWrap(true, true);
    sprite.createSprite(160, 80);
    sprite.setTextWrap(true, true);
    sprite.fillSprite(TFT_BLACK);
    sprite.setCursor(0, 0);
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextFont(1);
    sprite.printf("SunFFB Joystick");
    sprite.pushSprite(0, 0);

    ffbHandler.init();

    // tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_AUTO};
    // tusb_init(0, &dev_init);
    // tusb_init();
    while (!tud_mounted()) delay(10);

    // if (tud_mounted())
    // {
    //     tud_disconnect();
    //     delay(100);
    //     tud_connect();
    // }
    // delay(250);

    gPositions = xQueueCreate(1, sizeof(int16_t) * NUM_AXIS);
    gForces = xQueueCreate(1, sizeof(int32_t) * NUM_AXIS);

    semaphoreFFBDeviceInput = xSemaphoreCreateBinary();
    semaphoreFFBReportHandler = xSemaphoreCreateBinary();

    tinyusb_hid_device_input_sem = xSemaphoreCreateBinary();
    tinyusb_hid_device_input_mutex = xSemaphoreCreateMutex();

    xSemaphoreGive(semaphoreFFBDeviceInput);
    xSemaphoreGive(semaphoreFFBReportHandler);

    xTaskCreatePinnedToCore(lcd_task, "LCD", 4096, nullptr, 1, nullptr, appCore);
    xTaskCreatePinnedToCore(joystick_task, "Joystick", 2048, nullptr, 2, nullptr, appCore);
    xTaskCreatePinnedToCore(force_calculation_task, "Force", 4096, nullptr, 2, nullptr, appCore);

    xTaskCreatePinnedToCore(send_force_task, "SendForce", 2048, nullptr, configMAX_PRIORITIES - 1, nullptr, protoCore);
    xTaskCreatePinnedToCore(receive_position_task, "ReceivePos", 2048, nullptr, configMAX_PRIORITIES - 1, nullptr, protoCore);
}

void loop()
{
    vTaskDelete(nullptr);
}