#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "creature/rtp/Protocol.h"
#include "creature/rtp/RtcpPacket.h"

namespace creatures::rtp {
namespace {

void appendU16(std::vector<uint8_t> &packet, uint16_t value) {
    packet.push_back(static_cast<uint8_t>(value >> 8U));
    packet.push_back(static_cast<uint8_t>(value));
}

void appendU32(std::vector<uint8_t> &packet, uint32_t value) {
    packet.push_back(static_cast<uint8_t>(value >> 24U));
    packet.push_back(static_cast<uint8_t>(value >> 16U));
    packet.push_back(static_cast<uint8_t>(value >> 8U));
    packet.push_back(static_cast<uint8_t>(value));
}

constexpr uint32_t TEST_SSRC = 0x0102'0304U;
constexpr const char *TEST_CNAME = "creature-server@test";

/**
 * Build the compound packet by hand, independently of the encoder.
 *
 * Deliberately not calling buildRtcpSenderReport(): if the fixture and the
 * encoder were the same code, a round-trip test would only prove the pair is
 * self-consistent, not that either matches the wire. This is the wire.
 */
std::vector<uint8_t> makeWirePacket(uint32_t synchronizationSource = TEST_SSRC,
                                    const std::string &canonicalName = TEST_CNAME) {
    std::vector<uint8_t> packet{0x80, 200, 0x00, 0x06};
    appendU32(packet, synchronizationSource);
    appendU32(packet, 0x1122'3344U);
    appendU32(packet, 0x5566'7788U);
    appendU32(packet, 0x99aa'bbccU);
    appendU32(packet, 123U);
    appendU32(packet, 45'678U);

    const std::size_t sdesStart = packet.size();
    packet.push_back(0x81);
    packet.push_back(202);
    appendU16(packet, 0);
    appendU32(packet, synchronizationSource);
    packet.push_back(1);
    packet.push_back(static_cast<uint8_t>(canonicalName.size()));
    packet.insert(packet.end(), canonicalName.begin(), canonicalName.end());
    packet.push_back(0);
    while ((packet.size() - sdesStart) % sizeof(uint32_t) != 0) {
        packet.push_back(0);
    }
    const std::size_t sdesWords = (packet.size() - sdesStart) / sizeof(uint32_t);
    packet[sdesStart + 2] = static_cast<uint8_t>((sdesWords - 1U) >> 8U);
    packet[sdesStart + 3] = static_cast<uint8_t>(sdesWords - 1U);
    return packet;
}

RtcpSenderReport testReport(uint32_t synchronizationSource = TEST_SSRC, const std::string &canonicalName = TEST_CNAME) {
    return RtcpSenderReport{
        .synchronizationSource = synchronizationSource,
        .ntpTimestamp = 0x1122'3344'5566'7788ULL,
        .rtpTimestamp = 0x99aa'bbccU,
        .packetCount = 123U,
        .octetCount = 45'678U,
        .canonicalName = canonicalName,
    };
}

} // namespace

// ---- The encoder and the wire ------------------------------------------------

TEST(RtcpPacket, EncoderMatchesTheWireFormat) { EXPECT_EQ(buildRtcpSenderReport(testReport()), makeWirePacket()); }

// ---- Round trips: the thing the two halves never had ------------------------

TEST(RtcpPacket, RoundTripsEveryField) {
    const auto original = testReport();
    const auto parsed = parseRtcpSenderReport(buildRtcpSenderReport(original));

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->synchronizationSource, original.synchronizationSource);
    EXPECT_EQ(parsed->ntpTimestamp, original.ntpTimestamp);
    EXPECT_EQ(parsed->rtpTimestamp, original.rtpTimestamp);
    EXPECT_EQ(parsed->packetCount, original.packetCount);
    EXPECT_EQ(parsed->octetCount, original.octetCount);
    EXPECT_EQ(parsed->canonicalName, original.canonicalName);
}

TEST(RtcpPacket, RoundTripsAtTheEdgesOfTheFields) {
    auto report = testReport();
    report.synchronizationSource = 0xFFFF'FFFFU;
    report.ntpTimestamp = 0xFFFF'FFFF'FFFF'FFFFULL;
    report.rtpTimestamp = 0xFFFF'FFFFU;
    report.packetCount = 0xFFFF'FFFFU;
    report.octetCount = 0xFFFF'FFFFU;

    const auto parsed = parseRtcpSenderReport(buildRtcpSenderReport(report));

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->ntpTimestamp, 0xFFFF'FFFF'FFFF'FFFFULL);
    EXPECT_EQ(parsed->octetCount, 0xFFFF'FFFFU);
}

TEST(RtcpPacket, RoundTripsCnamesOfEveryAlignment) {
    // The SDES chunk pads to a word boundary, so each length mod 4 is a
    // different padding case.
    for (std::size_t length = 1; length <= 8; ++length) {
        const std::string canonicalName(length, 'c');
        const auto parsed = parseRtcpSenderReport(buildRtcpSenderReport(testReport(TEST_SSRC, canonicalName)));

        ASSERT_TRUE(parsed.has_value()) << "CNAME length " << length;
        EXPECT_EQ(parsed->canonicalName, canonicalName) << "CNAME length " << length;
    }
}

TEST(RtcpPacket, RoundTripsTheLongestLegalCname) {
    const std::string canonicalName(255, 'c');
    const auto parsed = parseRtcpSenderReport(buildRtcpSenderReport(testReport(TEST_SSRC, canonicalName)));

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->canonicalName.size(), 255U);
}

TEST(RtcpPacket, KeepsSenderCountersIndependentPerReport) {
    // The builder must hold no state between calls: the server sends one report
    // per channel in a loop, and a counter leaking from one into the next would
    // misreport every channel after the first.
    auto first = testReport(1000U);
    first.packetCount = 30U;
    first.octetCount = 40U;
    auto second = testReport(1001U);
    second.packetCount = 300U;
    second.octetCount = 400U;

    const auto firstParsed = parseRtcpSenderReport(buildRtcpSenderReport(first));
    const auto secondParsed = parseRtcpSenderReport(buildRtcpSenderReport(second));

    ASSERT_TRUE(firstParsed.has_value());
    ASSERT_TRUE(secondParsed.has_value());
    EXPECT_EQ(firstParsed->packetCount, 30U);
    EXPECT_EQ(firstParsed->octetCount, 40U);
    EXPECT_EQ(secondParsed->packetCount, 300U);
    EXPECT_EQ(secondParsed->octetCount, 400U);

    // The shared timing fields are untouched by the per-channel counters
    EXPECT_EQ(firstParsed->ntpTimestamp, secondParsed->ntpTimestamp);
    EXPECT_EQ(firstParsed->rtpTimestamp, secondParsed->rtpTimestamp);
}

// ---- What the encoder refuses to put on the wire ---------------------------

TEST(RtcpPacket, EncoderRejectsWhatCannotBeSent) {
    EXPECT_THROW((void)buildRtcpSenderReport(testReport(0)), std::invalid_argument);
    EXPECT_THROW((void)buildRtcpSenderReport(testReport(TEST_SSRC, "")), std::invalid_argument);
    EXPECT_THROW((void)buildRtcpSenderReport(testReport(TEST_SSRC, std::string(256, 'c'))), std::invalid_argument);
}

// ---- What the parser tolerates, and must keep tolerating -------------------

TEST(RtcpPacket, ParsesTheWirePacket) {
    const auto report = parseRtcpSenderReport(makeWirePacket());

    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->synchronizationSource, TEST_SSRC);
    EXPECT_EQ(report->ntpTimestamp, 0x1122'3344'5566'7788ULL);
    EXPECT_EQ(report->rtpTimestamp, 0x99aa'bbccU);
    EXPECT_EQ(report->packetCount, 123U);
    EXPECT_EQ(report->octetCount, 45'678U);
    EXPECT_EQ(report->canonicalName, TEST_CNAME);
}

TEST(RtcpPacket, SafelySkipsUnknownCompoundBlocks) {
    auto packet = makeWirePacket();
    const auto sdes = packet.begin() + 28;
    std::vector<uint8_t> withUnknown(packet.begin(), sdes);
    withUnknown.insert(withUnknown.end(), {0x80, 204, 0x00, 0x01, 0x12, 0x34, 0x56, 0x78});
    withUnknown.insert(withUnknown.end(), sdes, packet.end());

    const auto report = parseRtcpSenderReport(withUnknown);

    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->synchronizationSource, TEST_SSRC);
}

TEST(RtcpPacket, AcceptsAndSkipsSenderReportReceiverBlocks) {
    auto packet = makeWirePacket();
    packet[0] = 0x81;
    packet[2] = 0x00;
    packet[3] = 0x0c;
    packet.insert(packet.begin() + 28, 24, 0);

    const auto report = parseRtcpSenderReport(packet);

    ASSERT_TRUE(report.has_value());
    EXPECT_EQ(report->packetCount, 123U);
}

// ---- What the parser rejects ------------------------------------------------

TEST(RtcpPacket, RejectsMalformedCompoundPackets) {
    auto truncated = makeWirePacket();
    truncated.pop_back();
    EXPECT_FALSE(parseRtcpSenderReport(truncated).has_value()) << "length not a multiple of 4";

    auto invalidVersion = makeWirePacket();
    invalidVersion[0] = 0x40;
    EXPECT_FALSE(parseRtcpSenderReport(invalidVersion).has_value()) << "version != 2";

    auto invalidLength = makeWirePacket();
    invalidLength[3] = 0x7f;
    EXPECT_FALSE(parseRtcpSenderReport(invalidLength).has_value()) << "length past the end";

    auto senderReportExtension = makeWirePacket();
    senderReportExtension[3] = 0x07;
    senderReportExtension.insert(senderReportExtension.begin() + 28, 4, 0);
    EXPECT_FALSE(parseRtcpSenderReport(senderReportExtension).has_value())
        << "profile-specific SR extension, deliberately refused";

    auto firstPacketIsNotSenderReport = makeWirePacket();
    firstPacketIsNotSenderReport[1] = 201;
    EXPECT_FALSE(parseRtcpSenderReport(firstPacketIsNotSenderReport).has_value()) << "compound must open with an SR";

    auto missingCname = makeWirePacket();
    missingCname[36] = 2;
    EXPECT_FALSE(parseRtcpSenderReport(missingCname).has_value()) << "SDES carries NAME but no CNAME";

    auto zeroSynchronizationSource = makeWirePacket(0);
    EXPECT_FALSE(parseRtcpSenderReport(zeroSynchronizationSource).has_value()) << "zero SSRC";

    const std::vector<uint8_t> oversized(MAX_RTCP_PACKET_SIZE + sizeof(uint32_t), 0);
    EXPECT_FALSE(parseRtcpSenderReport(oversized).has_value()) << "larger than the receive buffer";

    EXPECT_FALSE(parseRtcpSenderReport({}).has_value()) << "empty datagram";
}

TEST(RtcpPacket, RejectsMismatchedOrDuplicateCname) {
    auto mismatchedSource = makeWirePacket();
    mismatchedSource[32] ^= 0x01;
    EXPECT_FALSE(parseRtcpSenderReport(mismatchedSource).has_value()) << "CNAME belongs to another SSRC";

    auto duplicateCname = makeWirePacket();
    const std::size_t endItem = duplicateCname.size() - 2;
    duplicateCname[endItem] = 1;
    duplicateCname[endItem + 1] = 0;
    EXPECT_FALSE(parseRtcpSenderReport(duplicateCname).has_value()) << "a second, empty CNAME";
}

TEST(RtcpPacket, RejectsInvalidPadding) {
    auto invalidPadding = makeWirePacket();
    invalidPadding[28] |= 0x20;
    invalidPadding.back() = 0xff;
    EXPECT_FALSE(parseRtcpSenderReport(invalidPadding).has_value()) << "pad count larger than the block";

    auto paddingNotOnLastPacket = makeWirePacket();
    paddingNotOnLastPacket[0] |= 0x20;
    EXPECT_FALSE(parseRtcpSenderReport(paddingNotOnLastPacket).has_value()) << "padding on a non-final packet";

    auto truncatedItem = makeWirePacket(TEST_SSRC, "x");
    truncatedItem.back() = 1;
    EXPECT_FALSE(parseRtcpSenderReport(truncatedItem).has_value()) << "item type with no length byte after it";
}

TEST(RtcpPacket, RejectsNonZeroSdesChunkPadding) {
    // A 2-byte CNAME leaves three pad bytes in the chunk, so this is the case
    // the controller's own suite could not reach with a 1-byte name.
    auto nonZeroAlignment = makeWirePacket(TEST_SSRC, "ab");
    ASSERT_EQ(nonZeroAlignment.back(), 0) << "expected chunk padding to pad with";
    nonZeroAlignment.back() = 1;
    EXPECT_FALSE(parseRtcpSenderReport(nonZeroAlignment).has_value());
}

} // namespace creatures::rtp
