/**
 * Bounded timing counters and report scheduling, retaining maxima rather than complete event history.
 * Short critical sections or native mutexes protect counters; never perform I/O while holding them.
 * Application-observed events do not automatically measure USB or motor-side wire completion.
 */

#pragma once
#include <algorithm>
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

        TimingSnapshot take_window()
        {
            TimingGuard guard(lock);
            const TimingSnapshot result{count, maxGap, overruns, maxWork, failures};
            // Counts remain cumulative for rate deltas; maxima/errors describe this window.
            maxGap = overruns = maxWork = failures = 0;
            return result;
        }

        private:
        const uint32_t budget;
        uint32_t previous = 0;
        bool started = false;
        TimingLock lock;
        uint32_t count = 0, maxGap = 0, overruns = 0, maxWork = 0, failures = 0;
    };

    struct ForceDeadlineSnapshot
    {
        uint32_t count, maxWakeUs, maxLockUs, maxElapsedUs, missed, skipped;
    };

    struct ForceDeadlineEvent
    {
        uint32_t sequence, releaseUs, startUs, lockedUs, computedUs, endUs, context;
    };

    class ForceDeadlineStream
    {
        public:
        explicit ForceDeadlineStream(uint32_t periodUs) : period(periodUs)
        {
        }

        void record(uint32_t releaseUs, uint32_t startUs, uint32_t lockedUs, uint32_t endUs)
        {
            record_detail(releaseUs, startUs, lockedUs, endUs, endUs, 0);
        }

        void record_detail(uint32_t releaseUs, uint32_t startUs, uint32_t lockedUs,
                           uint32_t computedUs, uint32_t endUs, uint32_t context)
        {
            TimingGuard guard(lock);
            ++stat.count;
            if (hasPrevious)
            {
                const uint32_t periods = (releaseUs - previousReleaseUs) / period;
                if (periods > 1)
                {
                    stat.skipped += periods - 1;
                }
            }
            previousReleaseUs = releaseUs;
            hasPrevious = true;
            const uint32_t wake = startUs - releaseUs;
            const uint32_t waiting = lockedUs - startUs;
            const uint32_t elapsed = endUs - releaseUs;
            // Retain the exact worst event across windows, not unrelated maxima.
            if (worst.sequence == 0 || elapsed > uint32_t(worst.endUs - worst.releaseUs))
            {
                worst = {total.count + stat.count, releaseUs, startUs, lockedUs,
                         computedUs, endUs, context};
            }
            if (wake > stat.maxWakeUs)
            {
                stat.maxWakeUs = wake;
            }
            if (waiting > stat.maxLockUs)
            {
                stat.maxLockUs = waiting;
            }
            if (elapsed > stat.maxElapsedUs)
            {
                stat.maxElapsedUs = elapsed;
            }
            if (elapsed > period)
            {
                ++stat.missed;
            }
        }

        ForceDeadlineSnapshot take_window()
        {
            TimingGuard guard(lock);
            const auto result = stat;
            accumulate(total, stat);
            stat = {};
            return result;
        }

        // Independent readers cannot consume or erase another reader's evidence.
        ForceDeadlineSnapshot snapshot()
        {
            TimingGuard guard(lock);
            auto result = total;
            accumulate(result, stat);
            return result;
        }

        ForceDeadlineEvent worst_event()
        {
            TimingGuard guard(lock);
            return worst;
        }

        private:
        static void accumulate(ForceDeadlineSnapshot& target, const ForceDeadlineSnapshot& value)
        {
            target.count += value.count;
            target.missed += value.missed;
            target.skipped += value.skipped;
            target.maxWakeUs = std::max(target.maxWakeUs, value.maxWakeUs);
            target.maxLockUs = std::max(target.maxLockUs, value.maxLockUs);
            target.maxElapsedUs = std::max(target.maxElapsedUs, value.maxElapsedUs);
        }

        const uint32_t period;
        TimingLock lock;
        ForceDeadlineSnapshot stat{};
        ForceDeadlineSnapshot total{};
        ForceDeadlineEvent worst{};
        uint32_t previousReleaseUs = 0;
        bool hasPrevious = false;
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
