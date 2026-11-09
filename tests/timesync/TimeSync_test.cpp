#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "creature/timesync/TimeSync.h"

namespace creatures::timesync {

namespace {

using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Pair;

PtpReading lockedPtp() {
    PtpReading ptp;
    ptp.available = true;
    ptp.portState = "SLAVE";
    ptp.gmPresent = true;
    ptp.offsetNs = 482;
    ptp.gmIdentity = "380525.fffe.33ef92";
    return ptp;
}

ClockReading syncedClock() {
    ClockReading clock;
    clock.available = true;
    clock.synced = true;
    clock.maxErrorUs = 500;
    return clock;
}

TEST(TimeSync, SyncedWhenFollowingAGrandmasterWithADisciplinedClock) {
    const auto status = evaluateTimeSync(lockedPtp(), syncedClock());
    EXPECT_EQ(status.state, SyncState::Synced);
    EXPECT_THAT(status.reason, HasSubstr("380525.fffe.33ef92"));
}

TEST(TimeSync, UnsyncedWhenPtp4lIsntAnswering) {
    PtpReading ptp;
    ptp.error = "no reply";
    const auto status = evaluateTimeSync(ptp, syncedClock());
    EXPECT_EQ(status.state, SyncState::Unsynced);
    EXPECT_THAT(status.reason, HasSubstr("ptp4l isn't answering"));
}

TEST(TimeSync, UnsyncedWhenNotFollowingAGrandmaster) {
    auto ptp = lockedPtp();
    ptp.portState = "LISTENING";
    EXPECT_THAT(evaluateTimeSync(ptp, syncedClock()).reason, HasSubstr("LISTENING"));

    ptp = lockedPtp();
    ptp.gmPresent = false;
    EXPECT_EQ(evaluateTimeSync(ptp, syncedClock()).state, SyncState::Unsynced);
}

TEST(TimeSync, UnsyncedWhenTheOffsetIsTooLarge) {
    auto ptp = lockedPtp();
    ptp.offsetNs = -(PTP_MAX_OFFSET_NS + 1);
    EXPECT_EQ(evaluateTimeSync(ptp, syncedClock()).state, SyncState::Unsynced);

    ptp.offsetNs = PTP_MAX_OFFSET_NS;
    EXPECT_EQ(evaluateTimeSync(ptp, syncedClock()).state, SyncState::Synced);
}

TEST(TimeSync, ReasonsReadAsSentences) {
    auto clock = syncedClock();
    clock.synced = false;
    EXPECT_FALSE(evaluateTimeSync(lockedPtp(), clock).reason.ends_with("?"));
    clock = syncedClock();
    clock.maxErrorUs = CLOCK_MAX_ERROR_US + 1;
    EXPECT_FALSE(evaluateTimeSync(lockedPtp(), clock).reason.ends_with("?"));
}

TEST(TimeSync, UnsyncedWhenPhc2sysIsntDiscipliningTheClock) {
    auto clock = syncedClock();
    clock.synced = false;
    EXPECT_THAT(evaluateTimeSync(lockedPtp(), clock).reason, HasSubstr("phc2sys"));

    clock = syncedClock();
    clock.maxErrorUs = CLOCK_MAX_ERROR_US + 1;
    EXPECT_THAT(evaluateTimeSync(lockedPtp(), clock).reason, HasSubstr("hasn't been disciplined"));
}

TEST(TimeSync, TheGrandmasterHostCountsAsSynced) {
    auto ptp = lockedPtp();
    ptp.portState = "MASTER";
    ptp.gmPresent = false;
    EXPECT_EQ(evaluateTimeSync(ptp, syncedClock()).state, SyncState::Synced);
}

PtpReading grandmasterPtp() {
    PtpReading ptp;
    ptp.available = true;
    ptp.portState = "MASTER";
    ptp.gmPresent = false;
    ptp.gmIdentity = "380525.fffe.33ef92";
    return ptp;
}

PhcReading heldPhc() {
    PhcReading phc;
    phc.available = true;
    phc.offsetNs = 1'200;
    return phc;
}

TEST(TimeSync, TheInternalGrandMasterStateAlsoCountsAsAGrandmaster) {
    auto ptp = grandmasterPtp();
    ptp.portState = "GRAND_MASTER";
    EXPECT_EQ(evaluateTimeSync(ptp, syncedClock(), heldPhc()).state, SyncState::Synced);
}

TEST(TimeSync, AnUnreadPhcIsNotAFailure) {
    // Clients have no reason to look at the PHC, so no reading must not read as
    // a bad reading.
    EXPECT_EQ(evaluateTimeSync(lockedPtp(), syncedClock(), PhcReading{}).state, SyncState::Synced);
    EXPECT_EQ(evaluateTimeSync(grandmasterPtp(), syncedClock(), PhcReading{}).state, SyncState::Synced);
}

TEST(TimeSync, UnsyncedWhenPhc2sysHasStoppedHoldingThePhc) {
    // The failure this reading exists for: the server's own clock is fine, so
    // every other check passes, while the fleet follows a PHC that has drifted.
    auto phc = heldPhc();
    phc.offsetNs = PHC_MAX_OFFSET_NS + 1;

    const auto status = evaluateTimeSync(grandmasterPtp(), syncedClock(), phc);

    EXPECT_EQ(status.state, SyncState::Unsynced);
    EXPECT_THAT(status.reason, HasSubstr("phc2sys"));
    EXPECT_THAT(status.reason, HasSubstr("following a clock"));
}

TEST(TimeSync, APhcDriftingTheOtherWayIsAlsoUnsynced) {
    auto phc = heldPhc();
    phc.offsetNs = -(PHC_MAX_OFFSET_NS + 1);
    EXPECT_EQ(evaluateTimeSync(grandmasterPtp(), syncedClock(), phc).state, SyncState::Unsynced);
}

TEST(TimeSync, TheGrandmasterReasonBlamesNtpRatherThanPhc2sys) {
    // On a grandmaster, phc2sys runs CLOCK_REALTIME -> PHC, so an undisciplined
    // system clock is NTP's problem. Blaming phc2sys would send you to the wrong
    // daemon.
    ClockReading clock;
    clock.available = true;
    clock.synced = false;

    const auto grandmaster = evaluateTimeSync(grandmasterPtp(), clock, heldPhc());
    EXPECT_THAT(grandmaster.reason, HasSubstr("NTP"));
    EXPECT_THAT(grandmaster.reason, ::testing::Not(HasSubstr("phc2sys")));

    // A client's CLOCK_REALTIME really is phc2sys's job.
    const auto client = evaluateTimeSync(lockedPtp(), clock);
    EXPECT_THAT(client.reason, HasSubstr("phc2sys"));
}

TEST(TimeSync, TheGrandmasterReasonNamesThePhcOffsetWhenItHasOne) {
    const auto status = evaluateTimeSync(grandmasterPtp(), syncedClock(), heldPhc());
    ASSERT_EQ(status.state, SyncState::Synced);
    EXPECT_THAT(status.reason, HasSubstr("grandmaster"));
    EXPECT_THAT(status.reason, HasSubstr("1200"));
}

TEST(TimeSync, PhcOffsetReachesTxtRecordsWhenRead) {
    std::vector<std::pair<std::string, std::string>> txt;
    appendTimeSyncTxt(txt, evaluateTimeSync(grandmasterPtp(), syncedClock(), heldPhc()));
    EXPECT_THAT(txt, Contains(Pair("phc_offset_ns", "1200")));

    std::vector<std::pair<std::string, std::string>> clientTxt;
    appendTimeSyncTxt(clientTxt, evaluateTimeSync(lockedPtp(), syncedClock()));
    EXPECT_THAT(clientTxt, ::testing::Not(Contains(Pair("phc_offset_ns", ::testing::_))));
}

TEST(TimeSync, AddsTheStateToTxtRecords) {
    std::vector<std::pair<std::string, std::string>> txt;
    appendTimeSyncTxt(txt, evaluateTimeSync(lockedPtp(), syncedClock()));
    EXPECT_THAT(txt, Contains(Pair("ptp", "synced")));
    EXPECT_THAT(txt, Contains(Pair("ptp_port", "SLAVE")));
    EXPECT_THAT(txt, Contains(Pair("ptp_gm", "380525.fffe.33ef92")));
    EXPECT_THAT(txt, Contains(Pair("clock_synced", "yes")));

    // A clock phc2sys stopped correcting isn't synced, even before the kernel flags it
    auto staleClock = syncedClock();
    staleClock.maxErrorUs = CLOCK_MAX_ERROR_US + 1;
    std::vector<std::pair<std::string, std::string>> stale;
    appendTimeSyncTxt(stale, evaluateTimeSync(lockedPtp(), staleClock));
    EXPECT_THAT(stale, Contains(Pair("ptp", "unsynced")));
    EXPECT_THAT(stale, Contains(Pair("clock_synced", "no")));

    std::vector<std::pair<std::string, std::string>> unknown;
    appendTimeSyncTxt(unknown, unknownTimeSync("not Linux"));
    EXPECT_EQ(unknown, (std::vector<std::pair<std::string, std::string>>{{"ptp", "unknown"}}));
}

} // namespace

} // namespace creatures::timesync
