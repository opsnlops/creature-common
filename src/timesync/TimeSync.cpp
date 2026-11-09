#include <cstdlib>
#include <string>

#include <fmt/format.h>

#include "creature/timesync/TimeSync.h"

namespace creatures::timesync {

const char *syncStateName(SyncState state) {
    switch (state) {
    case SyncState::Synced:
        return "synced";
    case SyncState::Unsynced:
        return "unsynced";
    default:
        return "unknown";
    }
}

bool clockDisciplined(const ClockReading &clock) {
    return clock.available && clock.synced && clock.maxErrorUs <= CLOCK_MAX_ERROR_US;
}

const char *taiOffsetSourceName(TaiOffsetSource source) {
    switch (source) {
    case TaiOffsetSource::Ptp4l:
        return "ptp4l";
    case TaiOffsetSource::Kernel:
        return "kernel";
    case TaiOffsetSource::Assumed:
        return "assumed";
    default:
        return "unavailable";
    }
}

ResolvedTaiOffset resolveTaiOffset(const PtpReading &ptp, const int32_t kernelTaiSeconds,
                                   const std::optional<int32_t> configuredSeconds) {
    // ptp4l first: the grandmaster's own declaration, and the valid flag is it
    // saying whether to believe the number.
    if (ptp.available && ptp.utcOffsetValid) {
        return {ptp.utcOffsetSeconds, TaiOffsetSource::Ptp4l};
    }
    // Then the kernel, which chrony and ntpd set and systemd-timesyncd does not.
    if (kernelTaiSeconds != 0) {
        return {kernelTaiSeconds, TaiOffsetSource::Kernel};
    }
    // Then what the operator told us. Last, because an explicit setting should
    // not override a source that will actually vouch for itself.
    if (configuredSeconds) {
        return {*configuredSeconds, TaiOffsetSource::Assumed};
    }
    // Deliberately never ptp.utcOffsetSeconds with the valid flag clear, even
    // though it is usually right: a number nobody will vouch for, silently
    // trusted, is how the original 37-second bug comes back wearing a fix.
    return {};
}

bool phcDisciplined(const PhcReading &phc) {
    // An unavailable reading is not a failure: a client has no reason to look.
    return !phc.available || std::llabs(phc.offsetNs) <= PHC_MAX_OFFSET_NS;
}

TimeSyncStatus evaluateTimeSync(const PtpReading &ptp, const ClockReading &clock, const PhcReading &phc) {
    TimeSyncStatus status;
    status.ptp = ptp;
    status.clock = clock;
    status.phc = phc;
    status.state = SyncState::Unsynced;

    // linuxptp sends MASTER; GRAND_MASTER is its internal state, accepted in case
    // it ever reaches the wire (see portStateName).
    const bool isGrandmaster = ptp.available && (ptp.portState == "MASTER" || ptp.portState == "GRAND_MASTER");

    if (!ptp.available) {
        status.reason = fmt::format("ptp4l isn't answering: {}", ptp.error);
    } else if (ptp.portState != "SLAVE" && !isGrandmaster) {
        status.reason = fmt::format("the PTP port is {}, not following a grandmaster", ptp.portState);
    } else if (!isGrandmaster && !ptp.gmPresent) {
        status.reason = "ptp4l can't see a grandmaster";
    } else if (!isGrandmaster && std::llabs(ptp.offsetNs) > PTP_MAX_OFFSET_NS) {
        status.reason = fmt::format("the PTP clock is {} ns from the grandmaster, more than {} ns", ptp.offsetNs,
                                    PTP_MAX_OFFSET_NS);
        // Reported but not gated on for a grandmaster, because these answer a
        // client's question. On a client, phc2sys disciplines CLOCK_REALTIME
        // from the PHC and the kernel's PLL keeps maxerror tight, so STA_UNSYNC
        // and maxerror really are a phc2sys health check — CLOCK_MAX_ERROR_US is
        // calibrated for exactly that.
        //
        // A grandmaster gets CLOCK_REALTIME from NTP instead, and an SNTP client
        // like systemd-timesyncd steps and slews on a poll interval without
        // running the kernel PLL at all. maxerror then grows by 500 us a second
        // between polls however good the time is, and STA_UNSYNC may never be
        // cleared. Gating on them reported a healthy grandmaster as unsynced
        // for eight hours (#10).
        //
        // What matters on a grandmaster is checked above and below: ptp4l
        // answering, the port in MASTER, and phc2sys holding the PHC. Absolute
        // correctness of CLOCK_REALTIME does not affect the creatures anyway —
        // the whole chain being equally wrong keeps them in step with each other.
    } else if (!isGrandmaster && !clock.available) {
        status.reason = "unable to read the kernel's clock status";
    } else if (!isGrandmaster && !clock.synced) {
        status.reason = fmt::format("the system clock isn't synchronized, so phc2sys may not be running");
    } else if (!isGrandmaster && clock.maxErrorUs > CLOCK_MAX_ERROR_US) {
        status.reason = fmt::format(
            "the system clock hasn't been disciplined recently (max error {} ms), so phc2sys may not be running",
            clock.maxErrorUs / 1000);
    } else if (!phcDisciplined(phc)) {
        // Only a grandmaster reads this, and only it can fail here: the fleet is
        // following a PHC that has drifted away from the clock we schedule by.
        status.reason =
            fmt::format("the PTP hardware clock is {} ns from the system clock, more than {} ns, so phc2sys may not be "
                        "running — the creatures are following a clock this host is no longer holding",
                        phc.offsetNs, PHC_MAX_OFFSET_NS);
    } else {
        status.state = SyncState::Synced;
        if (isGrandmaster) {
            if (phc.available) {
                status.reason = fmt::format("this host is the PTP grandmaster, serving the PHC at {} ns", phc.offsetNs);
            } else if (!phc.error.empty()) {
                // Synced, but the one thing that can go wrong here is the one
                // thing we could not see. Say which, rather than implying we
                // checked it.
                status.reason =
                    fmt::format("this host is the PTP grandmaster; the PHC offset is unchecked ({})", phc.error);
            } else {
                status.reason = "this host is the PTP grandmaster";
            }
        } else {
            status.reason = fmt::format("following grandmaster {} at {} ns", ptp.gmIdentity, ptp.offsetNs);
        }
    }
    return status;
}

TimeSyncStatus unknownTimeSync(const std::string &reason) {
    TimeSyncStatus status;
    status.state = SyncState::Unknown;
    status.reason = reason;
    return status;
}

void appendTimeSyncTxt(std::vector<std::pair<std::string, std::string>> &txt, const TimeSyncStatus &status) {
    txt.emplace_back("ptp", syncStateName(status.state));
    if (status.ptp.available) {
        txt.emplace_back("ptp_port", status.ptp.portState);
        txt.emplace_back("ptp_gm", status.ptp.gmIdentity);
    }
    if (status.clock.available) {
        txt.emplace_back("clock_synced", clockDisciplined(status.clock) ? "yes" : "no");
    }
    if (status.phc.available) {
        txt.emplace_back("phc_offset_ns", std::to_string(status.phc.offsetNs));
        // Where the TAI correction came from, because an assumed offset and an
        // authoritative one should not look alike to whoever is reading this.
        txt.emplace_back("phc_tai_src", taiOffsetSourceName(status.phc.taiOffsetSource));
    }
}

} // namespace creatures::timesync
