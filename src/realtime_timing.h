/**
 * Bounded timing counters and report scheduling, retaining maxima rather than complete event history.
 * Short critical sections or native mutexes protect counters; never perform I/O while holding them.
 * Application-observed events do not automatically measure USB or motor-side wire completion.
 */

#pragma once
#if defined(ARDUINO_ARCH_ESP32)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

#else
#include <mutex>

#endif
#include <stdint.h>

namespace SunFFB
{
    // Snapshot only counters under a short critical section; never do I/O here.
    // S2 does not provide lock-free std::atomic<uint32_t> in this toolchain.
    class TimingLock
    {
        public:
        void lock() const
        {
#if defined(ARDUINO_ARCH_ESP32)
            portENTER_CRITICAL(&mux);
#else
            mux.lock();
#endif
        }

        void unlock() const
        {
#if defined(ARDUINO_ARCH_ESP32)
            portEXIT_CRITICAL(&mux);
#else
            mux.unlock();
#endif
        }

        private:
#if defined(ARDUINO_ARCH_ESP32)
        mutable portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#else
        mutable std::mutex mux;
#endif
    };

    class TimingGuard
    {
        public:
        explicit TimingGuard(const TimingLock& value) : lock(value)
        {
            lock.lock();
        }

        ~TimingGuard()
        {
            lock.unlock();
        }

        private:
        const TimingLock& lock;
    };

    struct TimingSnapshot
    {
        uint32_t count, maxGapUs, overBudget, maxWorkUs, failures;
    };

    class TimingStream
    {
        public:
        explicit TimingStream(uint32_t budgetUs) : budget(budgetUs)
        {
        }

        void record(uint32_t nowUs, uint32_t workUs = 0)
        {
            // The first event establishes the baseline; later unsigned gaps handle a natural micros wraparound.
            // workUs may be zero when measuring event cadence without per-event processing duration.
            TimingGuard guard(lock);
            if (started)
            {
                const uint32_t gap = nowUs - previous;
                if (gap > maxGap)
                    maxGap = gap;
                if (gap > budget)
                    ++overruns;
            }

            previous = nowUs;
            started = true;
            if (workUs > maxWork)
                maxWork = workUs;
            ++count;
        }

        void failed()
        {
            TimingGuard guard(lock);
            ++failures;
        }

        TimingSnapshot snapshot() const
        {
            TimingGuard guard(lock);
            return {count, maxGap, overruns, maxWork, failures};
        }

        private:
        const uint32_t budget;
        uint32_t previous = 0;
        bool started = false;
        TimingLock lock;
        uint32_t count = 0, maxGap = 0, overruns = 0, maxWork = 0, failures = 0;
    };

    // Keep the original phase after jitter; failed USB writes never advance it.
    class ReportSchedule
    {
        public:
        explicit ReportSchedule(uint32_t periodUs, uint32_t startUs = 0)
            : period(periodUs), previous(startUs)
        {
        }

        bool due(uint32_t nowUs) const
        {
            return uint32_t(nowUs - previous) >= period;
        }

        void sent(uint32_t nowUs)
        {
            // Skip missed periods while preserving phase; assigning previous=nowUs would accumulate drift.
            const uint32_t elapsed = nowUs - previous;
            previous += (elapsed / period) * period;
        }

        private:
        const uint32_t period;
        uint32_t previous;
    };
} // namespace SunFFB
