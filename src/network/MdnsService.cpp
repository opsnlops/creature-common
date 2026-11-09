#include <algorithm>
#include <cctype>
#include <string>

#include "creature/network/MdnsService.h"

namespace creatures::network {

std::string mdnsInstanceName(const std::string &name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

std::size_t fitTxtRecord(MdnsService &service) {
    static const std::string ellipsis = "...";
    std::size_t total = 0;

    for (auto &[key, value] : service.txt) {
        const std::size_t entry = key.size() + 1 + value.size();
        if (entry > MDNS_TXT_ENTRY_MAX_BYTES) {
            const std::size_t room = MDNS_TXT_ENTRY_MAX_BYTES - key.size() - 1;
            value = value.substr(0, room > ellipsis.size() ? room - ellipsis.size() : 0) + ellipsis;
            value.resize(std::min(value.size(), room));
        }
        total += 1 + key.size() + 1 + value.size();
    }

    return total;
}

} // namespace creatures::network
