#include <Arduino.h>
#include <FreeRTOS.h>
#include <Adafruit_TinyUSB.h>
#include <memory.h>
#include <algorithm>
#include <TFT_eSPI.h>
#include "ffb_report_types.h"
#include "ffb_report_descriptor.h"
#include "ffb_report_handler.h"
#include "ffb_device_input.h"
#include "ffb_force_calculator.h"
#include "simple_serial_communication.h"

// #define SERIAL_PRINT

#define VRX_PIN 18
#define VRY_PIN 17
#define SW_PIN 16
#define TDX_PIN 4
#define RDX_PIN 5

uint32_t tmp;

uint16_t offsetX = 0;
uint16_t offsetY = 0;

volatile uint32_t reportProcessTime = 0;
volatile uint32_t effectProcessTime = 0;

QueueHandle_t gPositions;
QueueHandle_t gForces;

HardwareSerial comSerial(1);

TFT_eSPI lcd = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite( & lcd);

Adafruit_USBD_HID usb_hid;
SunFFB::FFBDeviceInput ffbDeviceInput;
SunFFB::FFBReportHandler ffbHandler;
SunFFB::FFBForceCalculator ffbForceCalculator;

TaskHandle_t forceCalculationTaskHandle;
SemaphoreHandle_t semaphoreFFBDeviceInput;
SemaphoreHandle_t semaphoreFFBReportHandler;

static
const uint8_t ffbReportDescriptor[] PROGMEM = {
    // Windows expects all of the I/O/F reports to be wrapped in an application collection;
    // otherwise, the device won't be registered as capable of force-feedback.
    // Linux is fine either way.
    HID_USAGE_PAGE(HID_USAGE_PAGE_DESKTOP),
    HID_USAGE(HID_USAGE_DESKTOP_JOYSTICK),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),

    // Input Reports
    FFB_REPORT_DESC_INPUT_JOYSTICK,
    FFB_REPORT_DESC_INPUT_PID_STATE,
    // Output Reports
    FFB_REPORT_DESC_OUTPUT_SET_EFFECT,
    FFB_REPORT_DESC_OUTPUT_SET_ENVELOPE,
    FFB_REPORT_DESC_OUTPUT_SET_CONDITION,
    FFB_REPORT_DESC_OUTPUT_SET_PERIODIC,
    FFB_REPORT_DESC_OUTPUT_SET_CONSTANT,
    FFB_REPORT_DESC_OUTPUT_SET_RAMP,
    FFB_REPORT_DESC_OUTPUT_EFFECT_OPERATION,
    FFB_REPORT_DESC_OUTPUT_BLOCK_FREE,
    FFB_REPORT_DESC_OUTPUT_DEVICE_CONTROL,
    FFB_REPORT_DESC_OUTPUT_DEVICE_GAIN,
    // Feature Reports
    FFB_REPORT_DESC_FEATURE_CREATE_NEW_EFFECT,
    FFB_REPORT_DESC_FEATURE_BLOCK_LOAD,
    FFB_REPORT_DESC_FEATURE_POOL_REPORT,

    HID_COLLECTION_END
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
                    const SunFFB::BlockLoadReportData* data = ffbHandler.get_block_load_report_data();
                    memcpy(buffer, data, sizeof(SunFFB::BlockLoadReportData));

                    #ifdef SERIAL_PRINT
                    Serial.printf("Block load. idx: %d, status: %d \n", data -> effectBlockIndex, data -> blockLoadStatus);
                    #endif
                    return 4;
                }

                // Pool size, max simultaneous effects, etc.
                case REPORT_ID_POOL_REPORT:
                {
                    SunFFB::PoolReportData* data = ffbHandler.get_pool_report_data();
                    memcpy(buffer, data, sizeof(SunFFB::PoolReportData));

                    #ifdef SERIAL_PRINT
                    Serial.printf("Pool report. \n");
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

void hid_set_report_callback(uint8_t reportId, hid_report_type_t reportType, const uint8_t* buffer, uint16_t bufSize)
{
    uint32_t startTime = micros();

    xSemaphoreTake(semaphoreFFBReportHandler, portMAX_DELAY);
    switch (reportId)
    {
        case REPORT_ID_SET_EFFECT_REPORT:
            ffbHandler.set_effect((SunFFB::SetEffectReportData*)buffer);
        break;

        case REPORT_ID_SET_ENVELOPE_REPORT:
            ffbHandler.set_envelop((SunFFB::SetEnvelopeReportData*)buffer);
        break;

        case REPORT_ID_SET_CONDITION_REPORT:
            ffbHandler.set_condition((SunFFB::SetConditionReportData*)buffer);
        break;

        case REPORT_ID_SET_PERIODIC_REPORT:
            ffbHandler.set_periodic((SunFFB::SetPeriodicReportData*)buffer);
        break;

        case REPORT_ID_SET_CONSTANT_FORCE_REPORT:
            ffbHandler.set_constant_force((SunFFB::SetConstantForceReportData*)buffer);
        break;

        case REPORT_ID_SET_RAMP_FROCE_REPORT:
            ffbHandler.set_ramp_force((SunFFB::SetRampForceReportData*)buffer);
        break;

        case REPORT_ID_EFFECT_OPERATION_REPORT:
            ffbHandler.set_effect_operation((SunFFB::EffectOperationReportData*)buffer);
        break;

        case REPORT_ID_BLOCK_FREE_REPORT:
            ffbHandler.set_effect_block_free((SunFFB::BlockFreeReportData*)buffer);
        break;

        case REPORT_ID_DEVICE_CONTROL_REPORT:
            ffbHandler.set_device_control((SunFFB::DeviceControlReportData*)buffer);
        break;

        case REPORT_ID_DEVICE_GAIN_REPORT:
            ffbHandler.set_device_gain((SunFFB::DeviceGainReportData*)buffer);
        break;

        case REPORT_ID_CREATE_NEW_EFFECT_REPORT:
            ffbHandler.create_new_effect();
        break;

        default:
        break;
    }
    xSemaphoreGive(semaphoreFFBReportHandler);

    uint32_t endTime = micros();
    if (startTime < endTime)
        reportProcessTime = (endTime - startTime);
}

void lcd_task(void* params)
{
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        int32_t forces[NUM_AXIS] = {0};
        xQueuePeek(gForces, forces, 0);

        sprite.fillSprite(TFT_BLACK);
        sprite.drawRect(0, 0, 40, 40, TFT_RED);
        uint8_t x = ffbDeviceInput.inputData.axis[0] / float(USB_AXIS_MAX_ABSOLUTE) * 20 + 20;
        uint8_t y = ffbDeviceInput.inputData.axis[1] / float(USB_AXIS_MAX_ABSOLUTE) * 20 + 20;
        x = x < 2 ? 2 : x;
        x = x > 38 ? 38 : x;
        y = y < 2 ? 2 : y;
        y = y > 38 ? 38 : y;
        sprite.fillSmoothCircle(x, y, 2, TFT_GREEN, TFT_BLACK);

        sprite.setCursor(40, 0);
        sprite.printf("Report time: %d us", reportProcessTime);
        sprite.setCursor(40, 11);
        sprite.printf("Effect time: %d us", effectProcessTime);
        sprite.setCursor(40, 23);
        sprite.printf("AX: %d", ffbDeviceInput.inputData.axis[0]);
        sprite.setCursor(100, 23);
        sprite.printf("AY: %d", ffbDeviceInput.inputData.axis[1]);

        x = forces[0] / float(USB_MAX_MAGNITUDE) * 20 + 20;
        y = forces[1] / float(USB_MAX_MAGNITUDE) * 20 + 60;
        x = x < 2 ? 2 : x;
        x = x > 38 ? 38 : x;
        y = y < 42 ? 42 : y;
        y = y > 78 ? 78 : y;
        sprite.drawRect(0, 40, 40, 40, TFT_RED);
        sprite.fillSmoothCircle(x, y, 2, TFT_GREEN, TFT_BLACK);
        sprite.drawWideLine(20, 60, x, y, 2, TFT_GREEN, TFT_BLACK);

        sprite.setCursor(40, 35);
        sprite.printf("FX: %d", forces[0]);
        sprite.setCursor(100, 35);
        sprite.printf("FY: %d", forces[1]);

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
        vTaskDelayUntil( & wakeupTime, pdMS_TO_TICKS(40));
    }
}

void joystick_task(void* params)
{
    // ffbDeviceInput.reset();
    // TickType_t wakeupTime = xTaskGetTickCount();
    // while (true)
    // {
    //   int16_t coords[NUM_AXIS];
    //   coords[0] = analogRead(VRX_PIN) - offsetX;
    //   coords[1] = analogRead(VRY_PIN) - offsetY;
    //   coords[0] = std::clamp(coords[0], int16_t(-2048), int16_t(2048));
    //   coords[1] = std::clamp(coords[1], int16_t(-2048), int16_t(2048));

    //   coords[0] = coords[0] / 2048.f * USB_AXIS_MAX_ABSOLUTE;
    //   coords[1] = coords[1] / 2048.f * USB_AXIS_MAX_ABSOLUTE;

    //   xSemaphoreTake(semaphoreFFBDeviceInput, portMAX_DELAY);
    //   ffbDeviceInput.update_axis(coords, 50, 4);
    //   xSemaphoreGive(semaphoreFFBDeviceInput);

    //   usb_hid.sendReport(REPORT_ID_JOYSTICK, (void*)&ffbDeviceInput.inputData, sizeof(SunFFB::JoystickInputReportData));

    //   vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(2));
    // }

    ffbDeviceInput.reset();
    int32_t speedDeadBand[NUM_AXIS] = {30, 30};
    ffbDeviceInput.update_speed_deadband(speedDeadBand);
    
    TickType_t wakeupTime = xTaskGetTickCount();
    while (true)
    {
        int16_t coords[NUM_AXIS] = {0};
        xQueuePeek(gPositions, coords, portMAX_DELAY);

        xSemaphoreTake(semaphoreFFBDeviceInput, portMAX_DELAY);
        ffbDeviceInput.update_axis(coords, 0, 4);
        xSemaphoreGive(semaphoreFFBDeviceInput);

        usb_hid.sendReport(REPORT_ID_JOYSTICK, (void*)&ffbDeviceInput.inputData, sizeof(SunFFB::JoystickInputReportData));

        vTaskDelayUntil(&wakeupTime, pdMS_TO_TICKS(2));
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
    pinMode(VRX_PIN, INPUT);
    pinMode(VRY_PIN, INPUT);
    pinMode(SW_PIN, INPUT);
    uint32_t ox = 0;
    uint32_t oy = 0;
    for (auto i = 0; i < 1000; ++i)
    {
        ox += analogRead(VRX_PIN);
        oy += analogRead(VRY_PIN);
        delay(1);
    }
    offsetX = ox / 1000;
    offsetY = oy / 1000;

    lcd.init();
    lcd.setRotation(1);
    sprite.createSprite(160, 80);
    sprite.fillSprite(TFT_BLACK);
    sprite.setCursor(0, 0);
    sprite.setTextColor(TFT_WHITE);
    sprite.setTextFont(1);
    sprite.printf("SunFFB Joystick");
    sprite.pushSprite(0, 0);

    ffbHandler.init();

    // Manual begin() is required on core without built-in support e.g. mbed rp2040
    // TinyUSBDevice.setID(0xFFFF, 0x2010);
    TinyUSBDevice.setManufacturerDescriptor("SunFFB");
    TinyUSBDevice.setProductDescriptor("Force feedback joystick");
    if (!TinyUSBDevice.isInitialized())
        TinyUSBDevice.begin(0);

    // Setup HID
    usb_hid.setBootProtocol(HID_ITF_PROTOCOL_NONE);
    usb_hid.setPollInterval(2);
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

    gPositions = xQueueCreate(1, sizeof(int16_t) * NUM_AXIS);
    gForces = xQueueCreate(1, sizeof(int32_t) * NUM_AXIS);

    semaphoreFFBDeviceInput = xSemaphoreCreateBinary();
    semaphoreFFBReportHandler = xSemaphoreCreateBinary();

    xSemaphoreGive(semaphoreFFBDeviceInput);
    xSemaphoreGive(semaphoreFFBReportHandler);

    xTaskCreatePinnedToCore(lcd_task, "LCD", 4096, nullptr, 1, nullptr, appCore);
    xTaskCreatePinnedToCore(joystick_task, "Joystick", 2048, nullptr, 1, nullptr, appCore);
    xTaskCreatePinnedToCore(force_calculation_task, "Force", 4096, nullptr, 1, nullptr, appCore);

    xTaskCreatePinnedToCore(send_force_task, "SendForce", 2048, nullptr, configMAX_PRIORITIES - 1, nullptr, protoCore);
    xTaskCreatePinnedToCore(receive_position_task, "ReceivePos", 2048, nullptr, configMAX_PRIORITIES - 1, nullptr, protoCore);
}

void loop()
{
    vTaskDelete(nullptr);
}