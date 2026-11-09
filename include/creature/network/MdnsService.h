#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace creatures::network {

// One TXT entry is a length byte followed by key=value, so it can't exceed 255 bytes
inline constexpr std::size_t MDNS_TXT_ENTRY_MAX_BYTES = 255;

// Keep the whole TXT record small enough to fit in one network packet
inline constexpr std::size_t MDNS_TXT_RECORD_TARGET_BYTES = 1300;

/**
 * An mDNS service to advertise: the instance name, type, port, and TXT records
 */
struct MdnsService {
    std::string name;
    std::string type;
    uint16_t port = 0;
    std::vector<std::pair<std::string, std::string>> txt;
};

/**
 * An mDNS instance name, which we always publish in lower case
 */
std::string mdnsInstanceName(const std::string &name);

/**
 * Shorten any TXT values that would make an entry too long for DNS-SD
 *
 * @return the size of the whole TXT record in bytes once trimmed
 */
std::size_t fitTxtRecord(MdnsService &service);

} // namespace creatures::network
