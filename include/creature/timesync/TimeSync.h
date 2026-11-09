#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace creatures::timesync {

// Where ptp4l answers read-only management queries (uds_ro_address)
inline constexpr const char *PTP4L_READ_ONLY_SOCKET = "/var/run/ptp4lro";

// The PTP hardware clock must be this close to the grandmaster: a little over two
// samples at 48 kHz. Healthy hosts run at a few hundred nanoseconds.
inline constexpr int64_t PTP_MAX_OFFSET_NS = 50'000;

// The kernel grows maxerror by 500 microseconds a second until something
// disciplines the clock, so this catches phc2sys going quiet for about 20 seconds
inline constexpr long CLOCK_MAX_ERROR_US = 10'000;

// Where the PTP hardware clock lives. ptp4l is configured with phc_index -1, so
// it derives the device from the interface; this is a default rather than a fact,
// because the NIC has moved once already.
inline constexpr const char *DEFAULT_PHC_DEVICE = "/dev/ptp0";

// How far CLOCK_REALTIME and the PHC may drift apart before phc2sys is suspect.
// Generous next to PTP_MAX_OFFSET_NS: this is a software servo over a syscall
// pair, not hardware timestamping, and the read itself costs a microsecond or so.
inline constexpr int64_t PHC_MAX_OFFSET_NS = 1'000'000;

/**
 * What ptp4l reports about following the grandmaster
 */
struct PtpReading {
    bool available = false;
    std::string error; // Why it isn't available
    std::string portState;
    bool gmPresent = false;
    int64_t offsetNs = 0;
    std::string gmIdentity;
};

/**
 * What the kernel reports about the system clock, which phc2sys disciplines
 */
struct ClockReading {
    bool available = false;
    bool synced = false;
    long maxErrorUs = 0;
};

/**
 * How far the PTP hardware clock is from CLOCK_REALTIME
 *
 * This is what phc2sys exists to minimise, and which direction it runs matters.
 * On a client, phc2sys disciplines CLOCK_REALTIME *from* the PHC, so the kernel's
 * own synced flag already reflects its health. On a grandmaster it runs the other
 * way, CLOCK_REALTIME -> PHC, and nothing in the kernel's clock status says
 * anything about it: CLOCK_REALTIME stays NTP-disciplined and healthy while the
 * PHC the whole fleet follows drifts away from it. That failure is invisible
 * without this reading.
 */
struct PhcReading {
    bool available = false;
    std::string error;    // Why it isn't available
    int64_t offsetNs = 0; // PHC minus CLOCK_REALTIME
};

enum class SyncState { Synced, Unsynced, Unknown };

struct TimeSyncStatus {
    SyncState state = SyncState::Unknown;
    std::string reason;
    PtpReading ptp;
    ClockReading clock;
    PhcReading phc;
};

const char *syncStateName(SyncState state);

/**
 * Is phc2sys keeping the system clock in step: the kernel's synced flag is set and
 * the clock was corrected recently?
 */
bool clockDisciplined(const ClockReading &clock);

/**
 * Is phc2sys holding the PHC to CLOCK_REALTIME?
 *
 * Only meaningful where the host serves the PHC to others, which is why an
 * unavailable reading is not a failure: a client has no reason to look.
 */
bool phcDisciplined(const PhcReading &phc);

/**
 * Decide whether the host is synced. What that means depends on the host's role,
 * which the port state tells us.
 *
 * A client needs ptp4l following a grandmaster closely and phc2sys keeping
 * CLOCK_REALTIME in step. A grandmaster needs neither of those — it *is* the
 * reference — but it does need phc2sys holding the PHC to its own CLOCK_REALTIME,
 * because that PHC is what the entire fleet follows. RTCP scheduling needs
 * whichever applies.
 */
TimeSyncStatus evaluateTimeSync(const PtpReading &ptp, const ClockReading &clock, const PhcReading &phc = {});

/**
 * The status when this platform can't check it
 */
TimeSyncStatus unknownTimeSync(const std::string &reason);

/**
 * Add the sync state to an mDNS TXT record
 */
void appendTimeSyncTxt(std::vector<std::pair<std::string, std::string>> &txt, const TimeSyncStatus &status);

} // namespace creatures::timesync
