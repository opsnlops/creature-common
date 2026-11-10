#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "creature/logging/Logger.h"
#include "creature/timesync/TimeSync.h"

namespace creatures::timesync {

/**
 * Reads PTP and system clock status. Only Linux can check; elsewhere it's unknown.
 */
class TimeSyncMonitor {
  public:
    /**
     * @param ptp4lSocket ptp4l's read-only management socket
     * @param phcDevice the PTP hardware clock to compare against CLOCK_REALTIME,
     *        or empty to skip that reading. Clients have no reason to look,
     *        since their clock check already catches a stopped phc2sys; a
     *        grandmaster must, because the PHC is what the fleet follows. See
     *        phcDisciplined().
     */
    /**
     * @param configuredTaiOffsetSeconds the TAI-UTC offset to assume when
     *        neither ptp4l nor the kernel will supply one, or nothing to refuse
     *        to assume and report the PHC offset as unavailable instead
     */
    explicit TimeSyncMonitor(std::string ptp4lSocket = PTP4L_READ_ONLY_SOCKET, std::string phcDevice = {},
                             std::optional<int32_t> configuredTaiOffsetSeconds = DEFAULT_TAI_OFFSET_SECONDS);

    TimeSyncStatus check();

  private:
    PtpReading readPtp();
    ClockReading readClock();

    /**
     * @param ptp the PTP reading, for the TAI-to-UTC offset. A PHC runs on TAI
     *        and CLOCK_REALTIME on UTC, so without a trustworthy offset there
     *        is no comparison to make — and reporting one anyway is how this
     *        first shipped claiming 37 seconds of error on a healthy host.
     */
    PhcReading readPhc(const PtpReading &ptp);

    std::string ptp4lSocket;
    std::string phcDevice;
    std::optional<int32_t> configuredTaiOffsetSeconds;
#if defined(__linux__)
    // Only the Linux reader uses these, and -Wunused-private-field is on.
    uint16_t sequence = 0;
    uint32_t probe = 0;
#endif
};

/**
 * Checks sync every few seconds and logs it: when it changes, and again every
 * minute while it's lost. Audio keeps playing either way.
 */
class TimeSyncWatcher {
  public:
    TimeSyncWatcher(std::shared_ptr<Logger> logger, std::unique_ptr<TimeSyncMonitor> monitor);

    /**
     * Check if it's time, and log what changed
     *
     * @return the new status when the sync state changed, otherwise nothing
     */
    std::optional<TimeSyncStatus> poll();

    /**
     * The most recent status, checking now if there isn't one yet
     */
    const TimeSyncStatus &current();

  private:
    std::shared_ptr<Logger> logger;
    std::unique_ptr<TimeSyncMonitor> monitor;
    std::optional<TimeSyncStatus> last;
    std::chrono::steady_clock::time_point nextCheck{};
    std::chrono::steady_clock::time_point nextReminder{};
};

} // namespace creatures::timesync
