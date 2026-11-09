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

    // What disciplines CLOCK_REALTIME depends on the role, and so does the
    // advice. On a client it's phc2sys, pulling from the PHC. On a grandmaster
    // CLOCK_REALTIME is the top of the chain and NTP's job, while phc2sys runs
    // the other way to serve the PHC — so blaming phc2sys for an undisciplined
    // system clock there sends you to the wrong daemon.
    const char *clockKeeper = isGrandmaster ? "NTP" : "phc2sys";

    if (!ptp.available) {
        status.reason = fmt::format("ptp4l isn't answering: {}", ptp.error);
    } else if (ptp.portState != "SLAVE" && !isGrandmaster) {
        status.reason = fmt::format("the PTP port is {}, not following a grandmaster", ptp.portState);
    } else if (!isGrandmaster && !ptp.gmPresent) {
        status.reason = "ptp4l can't see a grandmaster";
    } else if (!isGrandmaster && std::llabs(ptp.offsetNs) > PTP_MAX_OFFSET_NS) {
        status.reason = fmt::format("the PTP clock is {} ns from the grandmaster, more than {} ns", ptp.offsetNs,
                                    PTP_MAX_OFFSET_NS);
    } else if (!clock.available) {
        status.reason = "unable to read the kernel's clock status";
    } else if (!clock.synced) {
        status.reason = fmt::format("the system clock isn't synchronized, so {} may not be running", clockKeeper);
    } else if (clock.maxErrorUs > CLOCK_MAX_ERROR_US) {
        status.reason =
            fmt::format("the system clock hasn't been disciplined recently (max error {} ms), so {} may not be running",
                        clock.maxErrorUs / 1000, clockKeeper);
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
            status.reason =
                phc.available ? fmt::format("this host is the PTP grandmaster, serving the PHC at {} ns", phc.offsetNs)
                              : "this host is the PTP grandmaster";
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
    }
}

} // namespace creatures::timesync
