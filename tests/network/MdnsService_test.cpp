#include <string>

#include <gtest/gtest.h>

#include "creature/network/MdnsService.h"

namespace creatures::network {

TEST(MdnsService, InstanceNameIsLowerCase) {
    EXPECT_EQ(mdnsInstanceName("Caroll"), "caroll");
    EXPECT_EQ(mdnsInstanceName("BEAKY"), "beaky");
    EXPECT_EQ(mdnsInstanceName("Light 1"), "light 1");
}

TEST(MdnsService, InstanceNameLeavesAnEmptyNameAlone) { EXPECT_EQ(mdnsInstanceName(""), ""); }

TEST(MdnsService, TrimsEntriesThatAreTooLong) {
    MdnsService service;
    service.txt = {{"short", "ok"}, {"long", std::string(400, 'x')}};

    const auto total = fitTxtRecord(service);

    EXPECT_EQ(service.txt[0].second, "ok");
    EXPECT_EQ(service.txt[1].first.size() + 1 + service.txt[1].second.size(), MDNS_TXT_ENTRY_MAX_BYTES);
    EXPECT_TRUE(service.txt[1].second.ends_with("..."));
    EXPECT_EQ(total, (1 + 8) + (1 + MDNS_TXT_ENTRY_MAX_BYTES));
}

TEST(MdnsService, LeavesEntriesThatAlreadyFitUntouched) {
    MdnsService service;
    service.txt = {{"txtvers", "1"}, {"version", "3.51.7"}};

    const auto total = fitTxtRecord(service);

    EXPECT_EQ(service.txt[0].second, "1");
    EXPECT_EQ(service.txt[1].second, "3.51.7");
    // Each entry is a length byte plus key=value
    EXPECT_EQ(total, (1 + 7 + 1 + 1) + (1 + 7 + 1 + 6));
}

TEST(MdnsService, AnEmptyRecordIsZeroBytes) {
    MdnsService service;
    EXPECT_EQ(fitTxtRecord(service), 0U);
}

TEST(MdnsService, FittingIsIdempotent) {
    MdnsService service;
    service.txt = {{"long", std::string(400, 'x')}};

    const auto first = fitTxtRecord(service);
    const auto trimmed = service.txt[0].second;
    const auto second = fitTxtRecord(service);

    EXPECT_EQ(first, second);
    EXPECT_EQ(service.txt[0].second, trimmed);
}

} // namespace creatures::network
