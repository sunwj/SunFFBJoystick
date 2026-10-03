#ifndef EMBEDDED_COMM_ESP32_TWAI_HAL_H
#define EMBEDDED_COMM_ESP32_TWAI_HAL_H

#include <atomic>
#include <cstring>
#include <driver/twai.h>
#include "CanFrame.h"

namespace EmbeddedComm
{
    struct CANDriverStats
    {
        uint32_t accepted, rejected, successAlerts, failureAlerts, busOffAlerts, recoveries,
            overflowAlerts;
    };

    // Arduino-ESP32 2.x / ESP-IDF 4.x TWAI. The peripheral requires an external transceiver.
    class Esp32TwaiHal
    {
        public:
        bool begin(int txPin, int rxPin, uint32_t bitrate, uint16_t receiveId = 0xFFFF,
                   bool singleShot = false)
        {
            if (mInstalled || (receiveId > 0x7FF && receiveId != 0xFFFF) || txPin == rxPin)
                return false;

            twai_timing_config_t timing{};
            switch (bitrate)
            {
                case 125000:
                    timing = TWAI_TIMING_CONFIG_125KBITS();
                    break;
                case 250000:
                    timing = TWAI_TIMING_CONFIG_250KBITS();
                    break;
                case 500000:
                    timing = TWAI_TIMING_CONFIG_500KBITS();
                    break;
                case 1000000:
                    timing = TWAI_TIMING_CONFIG_1MBITS();
                    break;
                default:
                    return false;
            }

            twai_general_config_t general =
                TWAI_GENERAL_CONFIG_DEFAULT(gpio_num_t(txPin), gpio_num_t(rxPin), TWAI_MODE_NORMAL);
            general.tx_queue_len =
                0; // No software TX backlog: retry backpressure in the application.
            general.rx_queue_len = 16;
            general.alerts_enabled = TWAI_ALERT_TX_SUCCESS | TWAI_ALERT_TX_FAILED |
                                     TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                                     TWAI_ALERT_RX_QUEUE_FULL;

            // 0xFFFF accepts all IDs; otherwise filter one standard ID.
            // Application validates RTR/extended format as needed.
            twai_filter_config_t filter{};
            filter.acceptance_code = receiveId == 0xFFFF ? 0 : uint32_t(receiveId) << 21;
            filter.acceptance_mask = receiveId == 0xFFFF ? 0xFFFFFFFFu : 0x001FFFFFu;
            filter.single_filter = true;
            if (twai_driver_install(&general, &timing, &filter) != ESP_OK)
                return false;

            mSingleShot = singleShot;
            mInstalled = true;
            if (twai_start() != ESP_OK)
            {
                twai_driver_uninstall();
                mInstalled = false;
                return false;
            }

            mRunning.store(true, std::memory_order_relaxed);
            return true;
        }

        bool send(const CANFrame& frame)
        {
            if (!mRunning.load(std::memory_order_relaxed) || frame.length > 8 ||
                frame.id > (frame.extended ? 0x1FFFFFFFu : 0x7FFu))
            {
                mRejected.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            twai_message_t message{};
            message.identifier = frame.id;
            message.data_length_code = frame.length;
            message.extd = frame.extended;
            message.rtr = frame.remote;
            message.ss = mSingleShot; // Default retries arbitration/errors; no software TX backlog.
            memcpy(message.data, frame.data, frame.length);
            if (twai_transmit(&message, 0) != ESP_OK)
            {
                mRejected.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            mAccepted.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        bool receive(CANFrame& frame)
        {
            if (!mRunning.load(std::memory_order_relaxed))
                return false;

            twai_message_t message{};
            if (twai_receive(&message, 0) != ESP_OK)
                return false;

            frame = {};
            frame.id = message.identifier;
            frame.length = message.data_length_code;
            frame.extended = message.extd;
            frame.remote = message.rtr;
            if (frame.length > 8)
                return true; // Codec rejects invalid DLC without reading past data.
            memcpy(frame.data, message.data, frame.length);
            return true;
        }

        // RX task owns recovery. Returns true on restart so application sequence history can reset.
        bool service()
        {
            if (!mInstalled)
                return false;

            uint32_t alerts = 0;
            twai_read_alerts(&alerts, 0);
            if (alerts & TWAI_ALERT_TX_SUCCESS)
                mSuccess.fetch_add(1, std::memory_order_relaxed);
            if (alerts & TWAI_ALERT_TX_FAILED)
                mFailure.fetch_add(1, std::memory_order_relaxed);
            if (alerts & TWAI_ALERT_RX_QUEUE_FULL)
                mOverflow.fetch_add(1, std::memory_order_relaxed);
            if (alerts & TWAI_ALERT_BUS_OFF)
            {
                mBusOff.fetch_add(1, std::memory_order_relaxed);
                mRunning.store(false, std::memory_order_relaxed);
            }

            if (mRunning.load(std::memory_order_relaxed))
                return false;

            twai_status_info_t status{};
            if (twai_get_status_info(&status) != ESP_OK)
                return false;

            if (status.state == TWAI_STATE_BUS_OFF)
            {
                twai_initiate_recovery();
            }
            else if (status.state == TWAI_STATE_STOPPED)
            {
                if (twai_start() == ESP_OK)
                {
                    mRunning.store(true, std::memory_order_relaxed);
                    mRecoveries.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
            }

            return false;
        }

        bool running() const
        {
            return mRunning.load(std::memory_order_relaxed);
        }

        CANDriverStats stats() const
        {
            return {mAccepted.load(), mRejected.load(),   mSuccess.load(), mFailure.load(),
                    mBusOff.load(),   mRecoveries.load(), mOverflow.load()};
        }

        private:
        bool mInstalled = false, mSingleShot = false;
        std::atomic<bool> mRunning{false};

        std::atomic<uint32_t> mAccepted{0}, mRejected{0}, mSuccess{0}, mFailure{0}, mBusOff{0},
            mRecoveries{0}, mOverflow{0};
    };
} // namespace EmbeddedComm
#endif
