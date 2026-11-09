#include <stdexcept>
#include <string>

#include <fmt/format.h>

#include "creature/rtp/Protocol.h"

namespace creatures::rtp {

std::string channelGroup(const uint8_t channel) {
    if (channel < MIN_CHANNEL || channel > MAX_CHANNEL) {
        throw std::out_of_range(
            fmt::format("audio channel {} is outside the valid range {}-{}", channel, MIN_CHANNEL, MAX_CHANNEL));
    }
    return fmt::format("{}{}", MULTICAST_PREFIX, channel);
}

std::string dialogGroup(const uint8_t channel) {
    if (channel < MIN_CHANNEL || channel > MAX_DIALOG_CHANNEL) {
        throw std::out_of_range(fmt::format("dialog channel {} is outside the valid range {}-{} (BGM is channel {})",
                                            channel, MIN_CHANNEL, MAX_DIALOG_CHANNEL, BGM_CHANNEL));
    }
    return channelGroup(channel);
}

std::string bgmGroup() { return channelGroup(BGM_CHANNEL); }

} // namespace creatures::rtp
