#include <optional>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "creature/timesync/TimeSync.h"

namespace creatures::timesync {

namespace {

using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::Key;
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

TEST(TimeSync, TaiOffsetPrefersPtp4lWhenItWillVouchForIt) {
    auto ptp = grandmasterPtp();
    ptp.utcOffsetSeconds = 37;
    ptp.utcOffsetValid = true;

    const auto resolved = resolveTaiOffset(ptp, 19, 99);

    ASSERT_TRUE(resolved.seconds.has_value());
    EXPECT_EQ(*resolved.seconds, 37) << "an authoritative source outranks both fallbacks";
    EXPECT_EQ(resolved.source, TaiOffsetSource::Ptp4l);
}

TEST(TimeSync, TaiOffsetFallsBackToTheKernel) {
    // chrony and ntpd set this; systemd-timesyncd leaves it at zero.
    auto ptp = grandmasterPtp();
    ptp.utcOffsetSeconds = 37;
    ptp.utcOffsetValid = false;

    const auto resolved = resolveTaiOffset(ptp, 37, 99);

    ASSERT_TRUE(resolved.seconds.has_value());
    EXPECT_EQ(*resolved.seconds, 37);
    EXPECT_EQ(resolved.source, TaiOffsetSource::Kernel) << "the kernel outranks an operator's assumption";
}

TEST(TimeSync, TaiOffsetUsesTheConfiguredValueLast) {
    // April's grandmaster: ptp4l announces 37 but will not vouch for it
    // (clockClass 248, internal oscillator), and timesyncd leaves the kernel
    // at zero. The operator's value is all that is left.
    auto ptp = grandmasterPtp();
    ptp.utcOffsetSeconds = 37;
    ptp.utcOffsetValid = false;

    // A configured value deliberately different from the announced one, so
    // this test can tell which was used rather than both being 37.
    const auto resolved = resolveTaiOffset(ptp, 0, 36);

    ASSERT_TRUE(resolved.seconds.has_value());
    EXPECT_EQ(*resolved.seconds, 36) << "the configured value, not the announcement ptp4l disowned";
    EXPECT_EQ(resolved.source, TaiOffsetSource::Assumed) << "and it is visibly an assumption";

    // And with nothing configured, the real default applies.
    const auto defaulted = resolveTaiOffset(ptp, 0, DEFAULT_TAI_OFFSET_SECONDS);
    ASSERT_TRUE(defaulted.seconds.has_value());
    EXPECT_EQ(*defaulted.seconds, 37);
}

TEST(TimeSync, TaiOffsetNeverSilentlyTrustsAnInvalidAnnouncement) {
    // The number ptp4l announces is usually right, and using it anyway is
    // exactly how the 37-second bug would return wearing a fix.
    auto ptp = grandmasterPtp();
    ptp.utcOffsetSeconds = 37;
    ptp.utcOffsetValid = false;

    const auto resolved = resolveTaiOffset(ptp, 0, std::nullopt);

    EXPECT_FALSE(resolved.seconds.has_value());
    EXPECT_EQ(resolved.source, TaiOffsetSource::Unavailable);
}

TEST(TimeSync, TaiOffsetSourcesAreNamed) {
    EXPECT_STREQ(taiOffsetSourceName(TaiOffsetSource::Ptp4l), "ptp4l");
    EXPECT_STREQ(taiOffsetSourceName(TaiOffsetSource::Kernel), "kernel");
    EXPECT_STREQ(taiOffsetSourceName(TaiOffsetSource::Assumed), "assumed");
    EXPECT_STREQ(taiOffsetSourceName(TaiOffsetSource::Unavailable), "unavailable");
}

TEST(TimeSync, LeapSecondsAbolishedSoTheDefaultIsThirtySeven) {
    // 37 since 2017; the CGPM resolved in 2022 to abolish leap seconds by 2035.
    // If this ever needs changing, the world got strange and so does the show.
    EXPECT_EQ(DEFAULT_TAI_OFFSET_SECONDS, 37);
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

TEST(TimeSync, AGrandmastersUndisciplinedSystemClockIsNotAFailure) {
    // The #10 bug. These fields answer a client's question: there, phc2sys
    // disciplines CLOCK_REALTIME from the PHC and the kernel PLL keeps maxerror
    // tight. A grandmaster gets CLOCK_REALTIME from NTP, and an SNTP client like
    // systemd-timesyncd leaves STA_UNSYNC set and lets maxerror grow by 500 us a
    // second between polls — so gating on them called a healthy grandmaster
    // unsynced for eight hours.
    ClockReading undisciplined;
    undisciplined.available = true;
    undisciplined.synced = false;
    undisciplined.maxErrorUs = CLOCK_MAX_ERROR_US * 1000; // hours of growth

    const auto grandmaster = evaluateTimeSync(grandmasterPtp(), undisciplined, heldPhc());
    EXPECT_EQ(grandmaster.state, SyncState::Synced);
    EXPECT_THAT(grandmaster.reason, HasSubstr("grandmaster"));

    // Unreadable is fine too — there is nothing there a grandmaster depends on.
    EXPECT_EQ(evaluateTimeSync(grandmasterPtp(), ClockReading{}, heldPhc()).state, SyncState::Synced);
}

TEST(TimeSync, AClientsUndisciplinedSystemClockStillIsAFailure) {
    // The other half: unchanged, because on a client it genuinely is phc2sys.
    ClockReading undisciplined;
    undisciplined.available = true;
    undisciplined.synced = false;

    const auto client = evaluateTimeSync(lockedPtp(), undisciplined);
    EXPECT_EQ(client.state, SyncState::Unsynced);
    EXPECT_THAT(client.reason, HasSubstr("phc2sys"));

    ClockReading stale;
    stale.available = true;
    stale.synced = true;
    stale.maxErrorUs = CLOCK_MAX_ERROR_US + 1;
    EXPECT_EQ(evaluateTimeSync(lockedPtp(), stale).state, SyncState::Unsynced);
}

TEST(TimeSync, TheGrandmasterReasonNamesThePhcOffsetWhenItHasOne) {
    const auto status = evaluateTimeSync(grandmasterPtp(), syncedClock(), heldPhc());
    ASSERT_EQ(status.state, SyncState::Synced);
    EXPECT_THAT(status.reason, HasSubstr("grandmaster"));
    EXPECT_THAT(status.reason, HasSubstr("1200"));
}

TEST(TimeSync, TxtRecordsSayWhereTheTaiCorrectionCameFrom) {
    auto phc = heldPhc();
    phc.taiOffsetSource = TaiOffsetSource::Assumed;

    std::vector<std::pair<std::string, std::string>> txt;
    appendTimeSyncTxt(txt, evaluateTimeSync(grandmasterPtp(), syncedClock(), phc));

    EXPECT_THAT(txt, Contains(Pair("phc_tai_src", "assumed")));
}

TEST(TimeSync, AGrandmasterDoesNotPublishAClientsClockVerdict) {
    // clock_synced comes from clockDisciplined(), which is calibrated for a
    // client. On a grandmaster it answers nothing, and publishing "no" on a
    // host whose time is fine was actively misleading.
    ClockReading undisciplined;
    undisciplined.available = true;
    undisciplined.synced = false;

    std::vector<std::pair<std::string, std::string>> txt;
    appendTimeSyncTxt(txt, evaluateTimeSync(grandmasterPtp(), undisciplined, heldPhc()));

    EXPECT_THAT(txt, ::testing::Not(Contains(Key("clock_synced"))));
    // Its real health still is published.
    EXPECT_THAT(txt, Contains(Pair("ptp_port", "MASTER")));
    EXPECT_THAT(txt, Contains(Key("phc_offset_ns")));
}

TEST(TimeSync, AClientStillPublishesItsClockVerdict) {
    std::vector<std::pair<std::string, std::string>> txt;
    appendTimeSyncTxt(txt, evaluateTimeSync(lockedPtp(), syncedClock()));
    EXPECT_THAT(txt, Contains(Pair("clock_synced", "yes")));
}

TEST(TimeSync, TheGrandmasterPredicateAcceptsBothMasterStates) {
    auto ptp = grandmasterPtp();
    EXPECT_TRUE(isGrandmaster(ptp));
    ptp.portState = "GRAND_MASTER";
    EXPECT_TRUE(isGrandmaster(ptp));
    ptp.portState = "SLAVE";
    EXPECT_FALSE(isGrandmaster(ptp));
    ptp.available = false;
    ptp.portState = "MASTER";
    EXPECT_FALSE(isGrandmaster(ptp)) << "an unavailable reading asserts nothing";
}

TEST(TimeSync, ThePhcThresholdCatchesAStoppedPhc2sysInMinutesNotHours) {
    // Measured on the mainstage server: 31.5 ns/s with phc2sys stopped. The
    // threshold exists to notice the fault, so the time to trip matters.
    constexpr double driftNsPerSecond = 31.5;
    const double minutesToTrip = (static_cast<double>(PHC_MAX_OFFSET_NS) / driftNsPerSecond) / 60.0;
    EXPECT_LT(minutesToTrip, 60.0) << "a dead phc2sys should be noticed within the hour";
    EXPECT_GT(PHC_MAX_OFFSET_NS, 10'000) << "but not so tight that a healthy host trips it";
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
