#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "creature/rtp/Protocol.h"

namespace creatures::rtp {

TEST(RtpProtocol, ChannelGroupsAreTheAddressesInUse) {
    // These are the groups both sides have been using. If this test changes,
    // every bird and the server have to change with it.
    EXPECT_EQ(channelGroup(1), "239.19.63.1");
    EXPECT_EQ(channelGroup(9), "239.19.63.9");
    EXPECT_EQ(channelGroup(16), "239.19.63.16");
    EXPECT_EQ(channelGroup(17), "239.19.63.17");
}

TEST(RtpProtocol, BgmIsChannelSeventeen) {
    EXPECT_EQ(BGM_CHANNEL, 17);
    EXPECT_EQ(bgmGroup(), "239.19.63.17");
    EXPECT_EQ(bgmGroup(), channelGroup(BGM_CHANNEL));
}

TEST(RtpProtocol, ChannelGroupRejectsOutOfRange) {
    EXPECT_THROW((void)channelGroup(0), std::out_of_range);
    EXPECT_THROW((void)channelGroup(18), std::out_of_range);
    EXPECT_THROW((void)channelGroup(255), std::out_of_range);
}

TEST(RtpProtocol, DialogGroupExcludesBgm) {
    EXPECT_EQ(dialogGroup(1), "239.19.63.1");
    EXPECT_EQ(dialogGroup(16), "239.19.63.16");
    EXPECT_THROW((void)dialogGroup(0), std::out_of_range);
    EXPECT_THROW((void)dialogGroup(BGM_CHANNEL), std::out_of_range);
}

TEST(RtpProtocol, EveryChannelHasAGroup) {
    for (uint8_t channel = MIN_CHANNEL; channel <= MAX_CHANNEL; ++channel) {
        EXPECT_NO_THROW((void)channelGroup(channel)) << "channel " << static_cast<int>(channel);
    }
}

TEST(RtpProtocol, TransportAndFramingMatchWhatBothSidesUsed) {
    EXPECT_EQ(RTP_PORT, 5004);
    EXPECT_EQ(RTCP_PORT, 5005);
    EXPECT_EQ(OPUS_PAYLOAD_TYPE, 96);
    EXPECT_EQ(SAMPLE_RATE, 48'000U);
    EXPECT_EQ(FRAME_MS, 10);
    EXPECT_EQ(SAMPLES_PER_FRAME, 480);
    EXPECT_EQ(CHANNEL_COUNT, 17);
}

} // namespace creatures::rtp
