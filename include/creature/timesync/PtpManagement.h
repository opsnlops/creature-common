#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace creatures::timesync {

// linuxptp management ids we read. TIME_STATUS_NP is a linuxptp extension.
inline constexpr uint16_t PTP_MGMT_TIME_STATUS_NP = 0xC000;
inline constexpr uint16_t PTP_MGMT_TIME_PROPERTIES_DATA_SET = 0x2003;
inline constexpr uint16_t PTP_MGMT_PORT_DATA_SET = 0x2004;

/**
 * Build a PTP management GET message, as linuxptp's pmc sends to ptp4l
 *
 * @param managementId which dataset to read
 * @param sequenceId matches a response to its request
 * @param clientId fills our source port identity so replies can be told apart
 */
std::vector<uint8_t> encodeManagementGet(uint16_t managementId, uint16_t sequenceId, uint64_t clientId);

/**
 * The dataset carried by a management RESPONSE
 */
struct ManagementResponse {
    uint16_t managementId = 0;
    std::vector<uint8_t> data;
};

/**
 * Decode a management RESPONSE, or nothing if it isn't one (including errors)
 */
std::optional<ManagementResponse> decodeManagementResponse(const uint8_t *message, size_t length);

/**
 * The parts of linuxptp's TIME_STATUS_NP dataset we use
 */
struct TimeStatus {
    int64_t masterOffsetNs = 0;
    bool gmPresent = false;
    std::string gmIdentity; // As pmc prints it, e.g. 380525.fffe.33ef92
};

std::optional<TimeStatus> decodeTimeStatus(const std::vector<uint8_t> &data);

/**
 * The port state from a PORT_DATA_SET, such as SLAVE or LISTENING
 */
std::optional<std::string> decodePortState(const std::vector<uint8_t> &data);

/**
 * What TIME_PROPERTIES_DATA_SET says about converting PTP time to UTC
 *
 * A PTP clock runs on TAI. Anything comparing it against CLOCK_REALTIME needs
 * this offset, and needs to know whether the grandmaster will vouch for it.
 */
struct TimeProperties {
    int16_t currentUtcOffsetSeconds = 0;
    bool currentUtcOffsetValid = false;
    bool ptpTimescale = false; ///< Set when the clock really is on TAI rather than arbitrary
};

std::optional<TimeProperties> decodeTimeProperties(const std::vector<uint8_t> &data);

} // namespace creatures::timesync
