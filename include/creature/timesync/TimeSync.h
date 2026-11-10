#pragma once

#include <cstdint>
#include <optional>
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
// disciplines the clock (measured at 499 on the grandmaster, where nothing reset
// it for ten minutes), so this catches phc2sys going quiet for about 20 seconds.
// On a client phc2sys resets it every second while it runs: Beaky's stayed
// between 0 and 500 us over 20,000 reads, with no NTP daemon running (2026-11-09).
inline constexpr long CLOCK_MAX_ERROR_US = 10'000;

// Where the PTP hardware clock lives. ptp4l is configured with phc_index -1, so
// it derives the device from the interface; this is a default rather than a fact,
// because the NIC has moved once already.
inline constexpr const char *DEFAULT_PHC_DEVICE = "/dev/ptp0";

// How far CLOCK_REALTIME and the PHC may drift apart before phc2sys is suspect.
//
// 50 us, chosen from measurement rather than reasoning. With phc2sys stopped on
// the mainstage server the PHC drifts at 31.5 ns/s -- 0.032 ppm, a far better
// oscillator than the 1-10 ppm this was first guessed against. The original
// 1 ms would therefore have taken 8.8 hours to trip, so a dead phc2sys went
// unreported for most of a day. 50 us trips in about 26 minutes.
//
// Why not tighter, to notice sooner: each check is a single read, and one noisy
// read raises the alarm. Read noise, not drift, sets the floor. The PHC is read
// between two CLOCK_REALTIME reads, so a reading can be off by up to half the
// time that bracket took.
//
// Measured on the grandmaster (I226) with phc2sys healthy, 20,000 reads 1 ms
// apart and 600 reads 1 s apart over ten minutes (2026-11-09): offset median
// +2.3 to +3.0 us, worst -6.4 us. The bracket took 6.7 to 8.5 us at the median
// and up to 26.5 us, enough for one read to be 13 us off on its own. 50 us is
// about 8 times the worst offset seen, and that worst read uses a quarter of it.
// At 20 us it would use two thirds; at 10 us one slow read could raise the alarm
// with phc2sys healthy.
//
// A Pi 5 measures much the same, so the cost is in reaching the PHC, not in the
// host. Beaky (RP1 over PCIe), 20,000 reads 1 ms apart: median +1.8 us, p99.9
// 3.6 us, worst -9.6 and +11.3 us, the bracket 5.7 us at the median and up to
// 26 us. Clients don't run this check (see phcDisciplined()), but if one ever
// does, 50 us holds there too.
//
// Still generous next to PTP_MAX_OFFSET_NS, because this is a software servo
// observed through a syscall pair rather than hardware timestamping.
//
// Note this threshold answers "has phc2sys stopped?" and not "is the error
// audible?". At that drift rate the creatures stay in step with each other
// regardless -- they all follow the same PHC -- and the error that accumulates
// is between audio playout and servo motion. Reaching a marginal 10 ms of that
// takes 87 hours, so detection is not the binding constraint; noticing the
// fault is.
inline constexpr int64_t PHC_MAX_OFFSET_NS = 50'000;

/**
 * The TAI-UTC offset to assume when nothing authoritative will say
 *
 * 37 since 2017, and effectively permanent: the CGPM resolved in 2022 to
 * abolish leap seconds by 2035, none has been inserted since 2016, and none is
 * scheduled. A default rather than a constant because "effectively permanent"
 * is not "guaranteed", and an operator who needs a different number should not
 * have to rebuild.
 */
inline constexpr int32_t DEFAULT_TAI_OFFSET_SECONDS = 37;

/**
 * Where a TAI-UTC offset came from
 *
 * Recorded because the whole of #10 was a number with no provenance: an
 * authoritative offset and an assumed one should not look alike in a log.
 */
enum class TaiOffsetSource {
    Unavailable, ///< Nothing would say, so no comparison was made
    Ptp4l,       ///< Announced by the grandmaster and flagged valid
    Kernel,      ///< From adjtimex, which chrony and ntpd set
    Assumed,     ///< The configured fallback; nothing authoritative was available
};

[[nodiscard]] const char *taiOffsetSourceName(TaiOffsetSource source);

struct ResolvedTaiOffset {
    std::optional<int32_t> seconds;
    TaiOffsetSource source = TaiOffsetSource::Unavailable;
};

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

    /// What ptp4l announces for converting its TAI timescale to UTC, and
    /// whether it will vouch for the number. Needed to compare a PHC against
    /// CLOCK_REALTIME at all.
    int16_t utcOffsetSeconds = 0;
    bool utcOffsetValid = false;
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
    int64_t offsetNs = 0; // PHC minus CLOCK_REALTIME, on the same timescale

    /**
     * The TAI-UTC offset subtracted to get there, in seconds
     *
     * Recorded rather than discarded, because leaving it out is how this went
     * wrong the first time: a PTP clock runs on TAI and CLOCK_REALTIME on UTC,
     * so an uncorrected comparison reported 37 seconds of error on a host whose
     * real offset was 3 microseconds. A reading that carries its own correction
     * cannot hide that again.
     */
    int32_t taiOffsetSeconds = 0;

    /// Where that correction came from, so an assumed offset is visible as one
    TaiOffsetSource taiOffsetSource = TaiOffsetSource::Unavailable;
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
 * Which TAI-UTC offset to trust, if any
 *
 * A PHC runs on TAI and CLOCK_REALTIME on UTC, so comparing them needs this.
 * Neither source is guaranteed: ptp4l announces an offset but only vouches for
 * it when configured with `utc_offset`, and the kernel's value is set by chrony
 * or ntpd but not by systemd-timesyncd. Returning nothing is a real answer —
 * better than a comparison wrong by the whole offset, which is how this first
 * shipped (#10).
 *
 * Authoritative sources first, the configured fallback last: if ptp4l vouches
 * for a number, an operator's guess should not override it.
 *
 * @param ptp what ptp4l said
 * @param kernelTaiSeconds the kernel's own offset, 0 when nothing has set it
 * @param configuredSeconds the operator's fallback, or nothing to refuse to assume
 */
[[nodiscard]] ResolvedTaiOffset resolveTaiOffset(const PtpReading &ptp, int32_t kernelTaiSeconds,
                                                 std::optional<int32_t> configuredSeconds);

/**
 * Is this host the grandmaster rather than a client?
 *
 * linuxptp sends MASTER; GRAND_MASTER is its internal state, accepted in case
 * it ever reaches the wire (see portStateName).
 */
[[nodiscard]] bool isGrandmaster(const PtpReading &ptp);

/**
 * Is phc2sys holding the PHC to CLOCK_REALTIME?
 *
 * Only meaningful where the host serves the PHC to others, which is why an
 * unavailable reading is not a failure: a client has no reason to look.
 *
 * phc2sys runs in opposite directions on the two roles, and each has its own
 * detector. On a client phc2sys steers CLOCK_REALTIME from the PHC and resets
 * the kernel's maxerror as it goes. If it stops, maxerror climbs and
 * clockDisciplined() fails within about 20 seconds, much sooner than this check
 * would. On a grandmaster phc2sys steers the PHC from CLOCK_REALTIME, which NTP
 * disciplines, so maxerror says nothing about phc2sys. This check is the only
 * one that notices there.
 *
 * Clients also can't easily look: /dev/ptp0 is root-only on the Pis, and the
 * controller runs unprivileged.
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
