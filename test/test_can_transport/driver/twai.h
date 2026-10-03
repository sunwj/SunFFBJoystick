#pragma once
#include <stdint.h>
#include <deque>
using esp_err_t = int;
using gpio_num_t = int;
using TickType_t = uint32_t;
constexpr int ESP_OK=0, ESP_FAIL=-1, TWAI_MODE_NORMAL=0;
constexpr uint32_t TWAI_ALERT_TX_SUCCESS=1, TWAI_ALERT_TX_FAILED=2, TWAI_ALERT_BUS_OFF=4,
    TWAI_ALERT_BUS_RECOVERED=8, TWAI_ALERT_RX_QUEUE_FULL=16;
enum { TWAI_STATE_STOPPED, TWAI_STATE_RUNNING, TWAI_STATE_BUS_OFF, TWAI_STATE_RECOVERING };
struct twai_timing_config_t { int rate=0; };
struct twai_general_config_t { int tx_io,rx_io,mode; uint32_t tx_queue_len=5,rx_queue_len=5,alerts_enabled=0; };
struct twai_filter_config_t { uint32_t acceptance_code=0,acceptance_mask=0;bool single_filter=false; };
struct twai_message_t { uint32_t identifier=0;uint8_t data_length_code=0;bool extd=false,rtr=false,ss=false;uint8_t data[8]{}; };
struct twai_status_info_t { int state=TWAI_STATE_STOPPED; };
#define TWAI_GENERAL_CONFIG_DEFAULT(tx,rx,mode) twai_general_config_t{tx,rx,mode}
#define TWAI_TIMING_CONFIG_125KBITS() twai_timing_config_t{125000}
#define TWAI_TIMING_CONFIG_250KBITS() twai_timing_config_t{250000}
#define TWAI_TIMING_CONFIG_500KBITS() twai_timing_config_t{500000}
#define TWAI_TIMING_CONFIG_1MBITS() twai_timing_config_t{1000000}
namespace TwaiStub {
inline twai_general_config_t general{};
inline twai_filter_config_t filter{};
inline twai_timing_config_t timing{};
inline twai_message_t lastTx{};
inline int state=TWAI_STATE_STOPPED, installs=0,uninstalls=0,starts=0,recoveries=0;
inline bool installFails=false,startFails=false,busy=false;
inline uint32_t alerts=0;
inline std::deque<twai_message_t> rx;
inline void reset(){state=TWAI_STATE_STOPPED;installs=uninstalls=starts=recoveries=0;installFails=startFails=busy=false;alerts=0;rx.clear();}
}
inline esp_err_t twai_driver_install(const twai_general_config_t* g,const twai_timing_config_t* t,const twai_filter_config_t* f){++TwaiStub::installs;TwaiStub::general=*g;TwaiStub::filter=*f;TwaiStub::timing=*t;return TwaiStub::installFails?ESP_FAIL:ESP_OK;}
inline esp_err_t twai_driver_uninstall(){++TwaiStub::uninstalls;return ESP_OK;}
inline esp_err_t twai_start(){++TwaiStub::starts;if(TwaiStub::startFails)return ESP_FAIL;TwaiStub::state=TWAI_STATE_RUNNING;TwaiStub::rx.clear();return ESP_OK;}
inline esp_err_t twai_transmit(const twai_message_t* m,TickType_t wait){if(wait || TwaiStub::busy || TwaiStub::state!=TWAI_STATE_RUNNING)return ESP_FAIL;TwaiStub::lastTx=*m;return ESP_OK;}
inline esp_err_t twai_receive(twai_message_t* m,TickType_t wait){if(wait || TwaiStub::rx.empty())return ESP_FAIL;*m=TwaiStub::rx.front();TwaiStub::rx.pop_front();return ESP_OK;}
inline esp_err_t twai_read_alerts(uint32_t* out,TickType_t wait){if(wait)return ESP_FAIL;*out=TwaiStub::alerts;TwaiStub::alerts=0;return ESP_OK;}
inline esp_err_t twai_get_status_info(twai_status_info_t* s){s->state=TwaiStub::state;return ESP_OK;}
inline esp_err_t twai_initiate_recovery(){++TwaiStub::recoveries;TwaiStub::state=TWAI_STATE_RECOVERING;return ESP_OK;}
