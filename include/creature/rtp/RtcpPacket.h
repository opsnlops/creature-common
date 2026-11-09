#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace creatures::rtp {

/**
 * One RTCP Sender Report, as it goes on the wire
 *
 * The sender and the receivers used to define this separately — the server had
 * the encoder, the controller had the decoder, and each side's understanding of
 * the format existed only in its own file. The only thing checking that they
 * agreed was the birds working. One struct and one encode/decode pair means a
 * drift can't be committed.
 *
 * `canonicalName` owns its bytes rather than borrowing them: receivers cache
 * reports by SSRC long after the datagram buffer is gone, and the CNAME is
 * load-bearing — it is how a receiver tells a fresh sender from the old one,
 * and a clock mapping change is refused unless the CNAMEs match and are
 * non-empty. A once-a-second packet does not need the allocation avoided.
 */
struct RtcpSenderReport {
    uint32_t synchronizationSource{0};
    uint64_t ntpTimestamp{0};
    uint32_t rtpTimestamp{0};
    uint32_t packetCount{0};
    uint32_t octetCount{0};
    std::string canonicalName;
};

/**
 * Build an RFC 3550 compound packet: one Sender Report followed by the
 * mandatory SDES CNAME chunk for the same SSRC.
 *
 * @throws std::invalid_argument if the SSRC is zero, or the CNAME is empty or
 *         longer than 255 bytes — none of which can be put on the wire.
 */
[[nodiscard]] std::vector<uint8_t> buildRtcpSenderReport(const RtcpSenderReport &report);

/**
 * Parse one RFC 3550 compound RTCP packet containing a Sender Report and an
 * SDES CNAME for the same SSRC.
 *
 * Deliberately tolerant of things real senders emit: reception report blocks
 * (RC > 0) are accepted and skipped, padding is accepted on the last packet of
 * the compound, unknown packet types after the first are skipped, and SDES may
 * carry several chunks, chunks for other SSRCs, and non-CNAME items.
 *
 * Deliberately strict about everything else, rejecting the whole datagram: a
 * version other than 2, a total length that is not a multiple of four or
 * exceeds MAX_RTCP_PACKET_SIZE, any truncation or bad length, a first packet
 * that is not a Sender Report, more than one Sender Report, a zero SSRC, a
 * Sender Report whose content length is not exactly 28 + RC*24 (which rejects
 * profile-specific extensions), non-zero SDES chunk padding, and anything other
 * than exactly one non-empty CNAME for the Sender Report's SSRC.
 *
 * @return the report, or nothing if the datagram is not one we accept
 */
[[nodiscard]] std::optional<RtcpSenderReport> parseRtcpSenderReport(std::span<const uint8_t> packet) noexcept;

} // namespace creatures::rtp
