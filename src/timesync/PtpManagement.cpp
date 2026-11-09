#include <array>

#include <fmt/format.h>

#include "creature/timesync/PtpManagement.h"

namespace creatures::timesync {

namespace {

constexpr size_t HEADER_BYTES = 34;
constexpr size_t MANAGEMENT_BODY_BYTES = 14; // Target port, boundary hops, action, reserved
constexpr size_t TLV_START = HEADER_BYTES + MANAGEMENT_BODY_BYTES;
constexpr uint8_t MESSAGE_TYPE_MANAGEMENT = 0x0D;
constexpr uint8_t ACTION_GET = 0;
constexpr uint8_t ACTION_RESPONSE = 2;
constexpr uint16_t TLV_MANAGEMENT = 0x0001;

void put16(std::vector<uint8_t> &out, uint16_t value) {
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value));
}

uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

uint32_t get32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | p[3];
}

uint64_t get64(const uint8_t *p) { return (static_cast<uint64_t>(get32(p)) << 32) | get32(p + 4); }

const char *portStateName(uint8_t state) {
    // 1-9 are IEEE 1588. 10 is linuxptp's internal PS_GRAND_MASTER, which it
    // normalizes to MASTER (6) before putting a PORT_DATA_SET on the wire —
    // confirmed against the live grandmaster with pmc. Named anyway, because if
    // a future linuxptp ever did leak it, the alternative is decoding to
    // "UNKNOWN" and reporting the grandmaster as unsynced forever.
    static constexpr std::array<const char *, 11> names = {"UNKNOWN",      "INITIALIZING", "FAULTY",      "DISABLED",
                                                           "LISTENING",    "PRE_MASTER",   "MASTER",      "PASSIVE",
                                                           "UNCALIBRATED", "SLAVE",        "GRAND_MASTER"};
    return state < names.size() ? names[state] : "UNKNOWN";
}

} // namespace

std::vector<uint8_t> encodeManagementGet(uint16_t managementId, uint16_t sequenceId, uint64_t clientId) {
    std::vector<uint8_t> message;
    message.reserve(TLV_START + 6);

    // Header
    message.push_back(MESSAGE_TYPE_MANAGEMENT);
    message.push_back(0x02); // PTP version 2
    put16(message, static_cast<uint16_t>(TLV_START + 6));
    message.push_back(0); // Domain 0, which is what pmc uses by default
    message.push_back(0);
    put16(message, 0);                           // Flags
    message.insert(message.end(), 8 + 4, 0);     // Correction field, message type specific
    for (int shift = 56; shift >= 0; shift -= 8) // Source port identity: clock identity, then port 0
        message.push_back(static_cast<uint8_t>(clientId >> shift));
    put16(message, 0);
    put16(message, sequenceId);
    message.push_back(0x04); // Control field: management
    message.push_back(0x7F); // Log message interval

    // Management body: every port, no boundary hops, GET
    message.insert(message.end(), 10, 0xFF);
    message.push_back(0);
    message.push_back(0);
    message.push_back(ACTION_GET);
    message.push_back(0);

    // MANAGEMENT TLV with just the id, as a GET carries no data
    put16(message, TLV_MANAGEMENT);
    put16(message, 2);
    put16(message, managementId);
    return message;
}

std::optional<ManagementResponse> decodeManagementResponse(const uint8_t *message, size_t length) {
    if (length < TLV_START + 6 || (message[0] & 0x0F) != MESSAGE_TYPE_MANAGEMENT || (message[1] & 0x0F) != 2) {
        return std::nullopt;
    }
    if ((message[HEADER_BYTES + 12] & 0x0F) != ACTION_RESPONSE) {
        return std::nullopt;
    }

    const uint16_t tlvType = get16(message + TLV_START);
    const uint16_t tlvLength = get16(message + TLV_START + 2);
    if (tlvType != TLV_MANAGEMENT || tlvLength < 2 || TLV_START + 4 + tlvLength > length) {
        return std::nullopt;
    }

    ManagementResponse response;
    response.managementId = get16(message + TLV_START + 4);
    response.data.assign(message + TLV_START + 6, message + TLV_START + 4 + tlvLength);
    return response;
}

std::optional<TimeStatus> decodeTimeStatus(const std::vector<uint8_t> &data) {
    // master_offset(8) ingress_time(8) cumulativeScaledRateOffset(4) scaledLastGmPhaseChange(4)
    // gmTimeBaseIndicator(2) lastGmPhaseChange(12) gmPresent(4) gmIdentity(8)
    if (data.size() < 50) {
        return std::nullopt;
    }
    TimeStatus status;
    status.masterOffsetNs = static_cast<int64_t>(get64(data.data()));
    status.gmPresent = get32(data.data() + 38) != 0;
    const uint8_t *id = data.data() + 42;
    status.gmIdentity = fmt::format("{:02x}{:02x}{:02x}.{:02x}{:02x}.{:02x}{:02x}{:02x}", id[0], id[1], id[2], id[3],
                                    id[4], id[5], id[6], id[7]);
    return status;
}

std::optional<TimeProperties> decodeTimeProperties(const std::vector<uint8_t> &data) {
    // currentUtcOffset(2) flags(1) timeSource(1)
    if (data.size() < 4) {
        return std::nullopt;
    }
    TimeProperties properties;
    properties.currentUtcOffsetSeconds = static_cast<int16_t>(get16(data.data()));
    const uint8_t flags = data[2];
    properties.currentUtcOffsetValid = (flags & 0x04U) != 0; // bit 2
    properties.ptpTimescale = (flags & 0x08U) != 0;          // bit 3
    return properties;
}

std::optional<std::string> decodePortState(const std::vector<uint8_t> &data) {
    // portIdentity(10) portState(1) ...
    if (data.size() < 11) {
        return std::nullopt;
    }
    return std::string(portStateName(data[10]));
}

} // namespace creatures::timesync
