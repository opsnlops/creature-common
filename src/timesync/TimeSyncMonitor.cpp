#include <utility>

#include "creature/timesync/TimeSyncMonitor.h"

#if defined(__linux__)
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/timex.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include <fmt/format.h>

#include "creature/timesync/PtpManagement.h"
#endif

namespace creatures::timesync {

namespace {
constexpr auto CHECK_INTERVAL = std::chrono::seconds(5);
constexpr auto REMINDER_INTERVAL = std::chrono::seconds(60);
} // namespace

TimeSyncMonitor::TimeSyncMonitor(std::string ptp4lSocket, std::string phcDevice,
                                 std::optional<int32_t> configuredTaiOffsetSeconds)
    : ptp4lSocket(std::move(ptp4lSocket)), phcDevice(std::move(phcDevice)),
      configuredTaiOffsetSeconds(configuredTaiOffsetSeconds) {}

#if defined(__linux__)

TimeSyncStatus TimeSyncMonitor::check() {
    const auto ptp = readPtp();
    return evaluateTimeSync(ptp, readClock(), readPhc(ptp));
}

PtpReading TimeSyncMonitor::readPtp() {
    PtpReading reading;

    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        reading.error = fmt::format("socket: {}", strerror(errno));
        return reading;
    }

    // ptp4l replies to our address. An abstract address needs no file, which the
    // service's read-only filesystem wouldn't allow.
    sockaddr_un local{};
    local.sun_family = AF_UNIX;
    const std::string name = fmt::format("creature-ptp-{}-{}", getpid(), probe++);
    std::memcpy(local.sun_path + 1, name.data(), name.size());
    const auto localLength = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + 1 + name.size());

    sockaddr_un remote{};
    remote.sun_family = AF_UNIX;
    std::strncpy(remote.sun_path, ptp4lSocket.c_str(), sizeof(remote.sun_path) - 1);

    timeval timeout{0, 500'000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    if (bind(fd, reinterpret_cast<sockaddr *>(&local), localLength) < 0) {
        reading.error = fmt::format("bind: {}", strerror(errno));
        close(fd);
        return reading;
    }

    bool haveTime = false;
    bool havePort = false;
    // TIME_PROPERTIES rides along on the socket we already have open. Its
    // absence is not fatal — only the PHC comparison needs it, and that
    // reports its own unavailability.
    for (const uint16_t id : {PTP_MGMT_TIME_STATUS_NP, PTP_MGMT_PORT_DATA_SET, PTP_MGMT_TIME_PROPERTIES_DATA_SET}) {
        const auto request = encodeManagementGet(id, sequence++, static_cast<uint64_t>(getpid()));
        if (sendto(fd, request.data(), request.size(), 0, reinterpret_cast<sockaddr *>(&remote), sizeof(remote)) < 0) {
            reading.error = fmt::format("{}: {}", ptp4lSocket, strerror(errno));
            close(fd);
            return reading;
        }

        uint8_t buffer[1500];
        const ssize_t received = recv(fd, buffer, sizeof(buffer), 0);
        if (received < 0) {
            reading.error = fmt::format("no reply from {}: {}", ptp4lSocket, strerror(errno));
            close(fd);
            return reading;
        }

        const auto response = decodeManagementResponse(buffer, static_cast<size_t>(received));
        if (!response) {
            continue;
        }
        if (response->managementId == PTP_MGMT_TIME_STATUS_NP) {
            if (const auto time = decodeTimeStatus(response->data)) {
                reading.offsetNs = time->masterOffsetNs;
                reading.gmPresent = time->gmPresent;
                reading.gmIdentity = time->gmIdentity;
                haveTime = true;
            }
        } else if (response->managementId == PTP_MGMT_PORT_DATA_SET) {
            if (const auto state = decodePortState(response->data)) {
                reading.portState = *state;
                havePort = true;
            }
        } else if (response->managementId == PTP_MGMT_TIME_PROPERTIES_DATA_SET) {
            if (const auto properties = decodeTimeProperties(response->data)) {
                reading.utcOffsetSeconds = properties->currentUtcOffsetSeconds;
                reading.utcOffsetValid = properties->currentUtcOffsetValid;
            }
        }
    }
    close(fd);

    reading.available = haveTime && havePort;
    if (!reading.available) {
        reading.error = fmt::format("unexpected replies from {}", ptp4lSocket);
    }
    return reading;
}

ClockReading TimeSyncMonitor::readClock() {
    ClockReading reading;
    timex status{};
    status.modes = 0; // Read only
    if (adjtimex(&status) == -1) {
        return reading;
    }
    reading.available = true;
    reading.synced = (status.status & STA_UNSYNC) == 0;
    reading.maxErrorUs = status.maxerror;
    return reading;
}

PhcReading TimeSyncMonitor::readPhc(const PtpReading &ptp) {
    PhcReading reading;
    if (phcDevice.empty()) {
        return reading; // Not asked to look
    }

    // A PHC runs on TAI and CLOCK_REALTIME on UTC, so the comparison is
    // meaningless without the offset between them. Two sources, and neither is
    // guaranteed: ptp4l announces one but only vouches for it when configured
    // with utc_offset, and the kernel's value is only set by chrony or ntpd --
    // systemd-timesyncd leaves it at zero. If neither will say, say nothing
    // rather than publish a number that is wrong by the whole offset.
    timex taiStatus{};
    taiStatus.modes = 0; // Read only
    const int32_t kernelTai = adjtimex(&taiStatus) != -1 ? static_cast<int32_t>(taiStatus.tai) : 0;
    const auto tai = resolveTaiOffset(ptp, kernelTai, configuredTaiOffsetSeconds);
    if (!tai.seconds) {
        reading.error = "no TAI-UTC offset to compare with: ptp4l will not vouch for one, the kernel's is unset, and "
                        "none is configured";
        return reading;
    }

    const int fd = open(phcDevice.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        reading.error = fmt::format("{}: {}", phcDevice, strerror(errno));
        return reading;
    }

    // A character device's clockid, as linuxptp's own FD_TO_CLOCKID does it.
    const clockid_t phcClock = static_cast<clockid_t>((~static_cast<unsigned int>(fd) << 3) | 3);

    // Read realtime either side of the PHC so the pair's own cost bounds the
    // error rather than hiding inside it.
    timespec before{};
    timespec phc{};
    timespec after{};
    const bool ok = clock_gettime(CLOCK_REALTIME, &before) == 0 && clock_gettime(phcClock, &phc) == 0 &&
                    clock_gettime(CLOCK_REALTIME, &after) == 0;
    if (!ok) {
        reading.error = fmt::format("{}: clock_gettime: {}", phcDevice, strerror(errno));
        close(fd);
        return reading;
    }
    close(fd);

    const auto nanos = [](const timespec &t) {
        return static_cast<int64_t>(t.tv_sec) * 1'000'000'000LL + static_cast<int64_t>(t.tv_nsec);
    };
    // Midpoint of the two realtime reads, so the sampling gap splits evenly
    // instead of landing wholly on one side of the offset.
    const int64_t realtime = (nanos(before) + nanos(after)) / 2;

    reading.available = true;
    reading.taiOffsetSeconds = *tai.seconds;
    reading.taiOffsetSource = tai.source;
    reading.offsetNs = nanos(phc) - realtime - static_cast<int64_t>(*tai.seconds) * 1'000'000'000LL;
    return reading;
}

#else

TimeSyncStatus TimeSyncMonitor::check() { return unknownTimeSync("PTP sync is only checked on Linux"); }
PtpReading TimeSyncMonitor::readPtp() { return {}; }
ClockReading TimeSyncMonitor::readClock() { return {}; }
PhcReading TimeSyncMonitor::readPhc(const PtpReading &) { return {}; }

#endif

TimeSyncWatcher::TimeSyncWatcher(std::shared_ptr<Logger> logger, std::unique_ptr<TimeSyncMonitor> monitor)
    : logger(std::move(logger)), monitor(std::move(monitor)) {}

const TimeSyncStatus &TimeSyncWatcher::current() {
    if (!last) {
        poll();
    }
    return *last;
}

std::optional<TimeSyncStatus> TimeSyncWatcher::poll() {
    const auto now = std::chrono::steady_clock::now();
    if (last && now < nextCheck) {
        return std::nullopt;
    }
    nextCheck = now + CHECK_INTERVAL;

    auto status = monitor->check();
    const bool changed = !last || last->state != status.state;

    if (changed) {
        switch (status.state) {
        case SyncState::Synced:
            logger->info("PTP: synced, {}", status.reason);
            break;
        case SyncState::Unsynced:
            logger->warn("PTP: not synced, {}. Audio keeps playing but may drift out of alignment.", status.reason);
            nextReminder = now + REMINDER_INTERVAL;
            break;
        case SyncState::Unknown:
            logger->info("PTP: {}", status.reason);
            break;
        }
    } else if (status.state == SyncState::Unsynced && now >= nextReminder) {
        logger->warn("PTP: still not synced, {}", status.reason);
        nextReminder = now + REMINDER_INTERVAL;
    }

    last = status;
    if (changed) {
        return status;
    }
    return std::nullopt;
}

} // namespace creatures::timesync
