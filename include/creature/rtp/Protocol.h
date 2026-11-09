#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace creatures::rtp {

/**
 * The RTP/RTCP wire contract, in one place
 *
 * These were defined twice — once in the controller's audio-config.h and once
 * in the server's config.h — under different names, with the multicast groups
 * built one way on one side and listed out on the other. The values agreed, but
 * nothing made them agree: a port or group changed on one side would compile
 * clean on both and take the audio down with nothing in any log to say why.
 *
 * What belongs here is contract: anything the sender and the receivers must
 * agree on byte for byte. Buffer sizes, jitter targets, playout delays and
 * bitrates are tuning, not contract — those stay with whoever is tuning them.
 */

// Transport
inline constexpr uint16_t RTP_PORT = 5004;
inline constexpr uint16_t RTCP_PORT = RTP_PORT + 1;
inline constexpr uint8_t OPUS_PAYLOAD_TYPE = 96; // dynamic PT, by agreement
inline constexpr std::size_t MAX_RTP_PACKET_SIZE = 2048;
inline constexpr std::size_t MAX_RTCP_PACKET_SIZE = 2048;

// Opus framing
inline constexpr uint32_t SAMPLE_RATE = 48'000;
inline constexpr uint16_t FRAME_MS = 10;

/**
 * Audio samples in one frame: 480 at 48 kHz / 10 ms.
 *
 * The controller called this FRAMES_PER_CHUNK and the server called it
 * RTP_SAMPLES. Neither name was right — they are samples, and they are per
 * frame — so this is the name going forward.
 */
inline constexpr uint16_t SAMPLES_PER_FRAME = static_cast<uint16_t>(SAMPLE_RATE * FRAME_MS / 1000);

/**
 * Channels are a uniform 1..17. Sixteen creature channels and BGM, which is
 * simply channel 17 and also the timing master — not a separate kind of thing.
 */
inline constexpr uint8_t MIN_CHANNEL = 1;
inline constexpr uint8_t CHANNEL_COUNT = 17;
inline constexpr uint8_t MAX_CHANNEL = CHANNEL_COUNT;
inline constexpr uint8_t MAX_DIALOG_CHANNEL = 16;
inline constexpr uint8_t BGM_CHANNEL = 17;

/** The multicast groups are this prefix plus the channel number */
inline constexpr const char *MULTICAST_PREFIX = "239.19.63.";

/**
 * The multicast group for a channel, 1..17
 *
 * @throws std::out_of_range if the channel is outside 1..17
 */
[[nodiscard]] std::string channelGroup(uint8_t channel);

/**
 * The multicast group for a dialog channel, 1..16
 *
 * A convenience over channelGroup() for callers that mean "not BGM" and want
 * that checked.
 *
 * @throws std::out_of_range if the channel is outside 1..16
 */
[[nodiscard]] std::string dialogGroup(uint8_t channel);

/** The multicast group background music goes out on */
[[nodiscard]] std::string bgmGroup();

} // namespace creatures::rtp
