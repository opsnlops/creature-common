#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "creature/timesync/PtpManagement.h"

namespace creatures::timesync {

namespace {

std::vector<uint8_t> fromHex(const std::string &hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return bytes;
}

// Captured from ptp4l 4.2 on Caroll through its read-only socket
const std::string TIME_STATUS_REPLY =
    "0d1200680000000000000000000000000000000098fe54fffe17e12100000000007f000000000000002a00000000020000010034"
    "c00000000000000001e218e5e5fdb81d1ee20000000000000000000000000000000000000000000000000001380525fffe33ef92";
const std::string PORT_DATA_SET_REPLY = "0d1200500000000000000000000000000000000098fe54fffe17e12100010001007f00000000"
                                        "0000002a0000000002000001001c200498fe54fffe17e121000109000000000000000000010300"
                                        "010002";
// The request that got those replies
const std::string TIME_STATUS_REQUEST = "0d02003600000000000000000000000000000000000000000000002a00000000047ffffffff"
                                        "fffffffffffff0000000000010002c000";

TEST(PtpManagement, EncodesTheSameGetAsTheWorkingRequest) {
    EXPECT_EQ(encodeManagementGet(PTP_MGMT_TIME_STATUS_NP, 0, 42), fromHex(TIME_STATUS_REQUEST));
}

TEST(PtpManagement, DecodesARealTimeStatusReply) {
    const auto bytes = fromHex(TIME_STATUS_REPLY);
    const auto response = decodeManagementResponse(bytes.data(), bytes.size());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->managementId, PTP_MGMT_TIME_STATUS_NP);

    const auto status = decodeTimeStatus(response->data);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(status->masterOffsetNs, 482);
    EXPECT_TRUE(status->gmPresent);
    EXPECT_EQ(status->gmIdentity, "380525.fffe.33ef92");
}

TEST(PtpManagement, DecodesARealPortDataSetReply) {
    const auto bytes = fromHex(PORT_DATA_SET_REPLY);
    const auto response = decodeManagementResponse(bytes.data(), bytes.size());
    ASSERT_TRUE(response.has_value());
    EXPECT_EQ(response->managementId, PTP_MGMT_PORT_DATA_SET);
    EXPECT_EQ(decodePortState(response->data), "SLAVE");
}

TEST(PtpManagement, DecodesTheGrandmasterPortState) {
    // The captured reply is from a client, so until now nothing had ever put a
    // master's port state through this decoder — which is the one link in the
    // grandmaster path that could have been dead on arrival. pmc against the
    // live server says the wire value is 6.
    const auto bytes = fromHex(PORT_DATA_SET_REPLY);
    const auto response = decodeManagementResponse(bytes.data(), bytes.size());
    ASSERT_TRUE(response.has_value());

    auto data = response->data;
    ASSERT_GE(data.size(), 11U);
    ASSERT_EQ(decodePortState(data), "SLAVE") << "the captured client reply";

    data[10] = 6;
    EXPECT_EQ(decodePortState(data), "MASTER") << "what the grandmaster actually sends";

    data[10] = 10;
    EXPECT_EQ(decodePortState(data), "GRAND_MASTER") << "linuxptp's internal state, if it ever leaked";
}

TEST(PtpManagement, UnknownPortStatesDecodeAsUnknown) {
    const auto bytes = fromHex(PORT_DATA_SET_REPLY);
    const auto response = decodeManagementResponse(bytes.data(), bytes.size());
    ASSERT_TRUE(response.has_value());
    auto data = response->data;
    ASSERT_GE(data.size(), 11U);

    for (const uint8_t state : {uint8_t{0}, uint8_t{11}, uint8_t{200}, uint8_t{255}}) {
        data[10] = state;
        EXPECT_EQ(decodePortState(data), "UNKNOWN") << "state " << static_cast<int>(state);
    }
}

TEST(PtpManagement, DecodesTimePropertiesAsTheGrandmasterReportsThem) {
    // The real mainstage grandmaster, via pmc: currentUtcOffset 37,
    // currentUtcOffsetValid 0, ptpTimescale 1, timeSource 0xa0. dataField is
    // currentUtcOffset(2) flags(1) timeSource(1).
    const std::vector<uint8_t> data = {0x00, 0x25, 0x08, 0xa0};

    const auto properties = decodeTimeProperties(data);

    ASSERT_TRUE(properties.has_value());
    EXPECT_EQ(properties->currentUtcOffsetSeconds, 37);
    EXPECT_FALSE(properties->currentUtcOffsetValid) << "not traceable, so it will not vouch for it";
    EXPECT_TRUE(properties->ptpTimescale) << "which is why the 37 matters at all";
}

TEST(PtpManagement, DecodesTimePropertiesWhenTheOffsetIsVouchedFor) {
    // Same, with bit 2 also set: a host configured to assert validity.
    const std::vector<uint8_t> data = {0x00, 0x25, 0x0c, 0xa0};

    const auto properties = decodeTimeProperties(data);

    ASSERT_TRUE(properties.has_value());
    EXPECT_EQ(properties->currentUtcOffsetSeconds, 37);
    EXPECT_TRUE(properties->currentUtcOffsetValid);
    EXPECT_TRUE(properties->ptpTimescale);
}

TEST(PtpManagement, DecodesANegativeUtcOffset) {
    // Int16, so signed. It was positive for every year this code will see, but
    // reading it unsigned would be a bug waiting for a different planet.
    const std::vector<uint8_t> data = {0xff, 0xff, 0x0c, 0xa0};
    const auto properties = decodeTimeProperties(data);
    ASSERT_TRUE(properties.has_value());
    EXPECT_EQ(properties->currentUtcOffsetSeconds, -1);
}

TEST(PtpManagement, RejectsAShortTimePropertiesDataField) {
    EXPECT_FALSE(decodeTimeProperties({}).has_value());
    EXPECT_FALSE(decodeTimeProperties({0x00, 0x25, 0x0c}).has_value());
}

TEST(PtpManagement, RejectsRequestsAndTruncatedMessages) {
    const auto request = fromHex(TIME_STATUS_REQUEST);
    EXPECT_FALSE(decodeManagementResponse(request.data(), request.size()).has_value());

    const auto reply = fromHex(TIME_STATUS_REPLY);
    EXPECT_FALSE(decodeManagementResponse(reply.data(), 60).has_value());
    EXPECT_FALSE(decodeTimeStatus(std::vector<uint8_t>(10, 0)).has_value());
    EXPECT_FALSE(decodePortState(std::vector<uint8_t>(5, 0)).has_value());
}

TEST(PtpManagement, RejectsErrorReplies) {
    auto reply = fromHex(TIME_STATUS_REPLY);
    reply[48] = 0x00;
    reply[49] = 0x02; // MANAGEMENT_ERROR_STATUS instead of MANAGEMENT
    EXPECT_FALSE(decodeManagementResponse(reply.data(), reply.size()).has_value());
}

} // namespace

} // namespace creatures::timesync
