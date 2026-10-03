#pragma once

// Hardware-in-the-loop instrumentation; included only in diagnostic builds.
#include <Arduino.h>
#include <atomic>
#include <esp_system.h>
#include "device/usbd.h"
#include "../src/realtime_timing.h"
#include "../src/motor_protocol/serial_link.h"

// Independent UART snapshots locate a stalled USB service.
inline std::atomic<uint32_t> hidCalls{0}, commandStarts{0}, commandEnds{0};
inline std::atomic<TaskHandle_t> usbServiceHandle{nullptr};
inline std::atomic<uint32_t> lcdStage{0};

#if MOTOR_TRANSPORT == 0
// Called by the single motor-UART TX owner; preserve IDs and field layouts.
template <typename Link>
void emit_hil_diagnostics(Link& link, SunFFB::ForceDeadlineStream& deadlinesStream,
                          TaskHandle_t forceTask, uint32_t lastHealthMs)
{
    // Test-only UART health survives loss of the USB diagnostics port.
    // One TX owner sends both records; production motor framing is unchanged.
    const uint32_t health[] = {lastHealthMs, uint32_t(esp_reset_reason()),
        uint32_t(tud_mounted()), uint32_t(tud_suspended()),
        uint32_t(ESP.getFreeHeap())};
    link.sendRaw(0x7E, reinterpret_cast<const uint8_t*>(health), sizeof(health),
                      SunFFB::SerialFrameFormat::Variable);
    const TaskHandle_t service = usbServiceHandle.load(std::memory_order_relaxed);
    const uint32_t progress[] = {lastHealthMs,
        0, 0, // Reserved legacy fields; preserve the diagnostic wire layout.
        hidCalls.load(std::memory_order_relaxed),
        commandStarts.load(std::memory_order_relaxed),
        commandEnds.load(std::memory_order_relaxed),
        service ? uint32_t(eTaskGetState(service)) : UINT32_MAX,
        service ? uint32_t(uxTaskGetStackHighWaterMark(service)) : 0};
    if ((lastHealthMs / 1000) % 2)
    {
        link.sendRaw(0x7D, reinterpret_cast<const uint8_t*>(progress), sizeof(progress),
                          SunFFB::SerialFrameFormat::Variable);
    }
    else
    {
        // Alternate equal-sized records to stay within the UART burst budget.
        const auto event = deadlinesStream.worst_event();
        const uint32_t detail[] = {lastHealthMs, event.sequence, event.releaseUs,
            event.startUs, event.lockedUs, event.computedUs, event.endUs, event.context};
        link.sendRaw(0x7A, reinterpret_cast<const uint8_t*>(detail), sizeof(detail),
                          SunFFB::SerialFrameFormat::Variable);
    }
    // Lifetime counters preserve evidence even if a UART record is lost.
    const auto deadline = deadlinesStream.snapshot();
    const uint32_t deadlines[] = {lastHealthMs, deadline.count, deadline.maxWakeUs,
        deadline.maxLockUs, deadline.maxElapsedUs, deadline.missed, deadline.skipped,
        uint32_t(FORCE_TASK_PERIOD_MS * 1000),
        forceTask
            ? uint32_t(uxTaskGetStackHighWaterMark(forceTask)) : 0};
    link.sendRaw(0x7C, reinterpret_cast<const uint8_t*>(deadlines), sizeof(deadlines),
                      SunFFB::SerialFrameFormat::Variable);
}
#endif
