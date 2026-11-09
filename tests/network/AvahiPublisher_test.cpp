#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "creature/logging/Logger.h"
#include "creature/network/AvahiPublisher.h"

namespace creatures::network {

namespace {

class QuietLogger : public Logger {
  public:
    void init(std::string) override {}
    void setLevel(const std::string &) override {}

  protected:
    void logTrace(const std::string &, fmt::format_args) override {}
    void logDebug(const std::string &, fmt::format_args) override {}
    void logInfo(const std::string &, fmt::format_args) override {}
    void logWarning(const std::string &, fmt::format_args) override {}
    void logError(const std::string &, fmt::format_args) override {}
    void logCritical(const std::string &, fmt::format_args) override {}
};

// Is any thread in this process named `name`?
bool threadNamed(const std::string &name) {
    for (const auto &task : std::filesystem::directory_iterator("/proc/self/task")) {
        std::ifstream comm(task.path() / "comm");
        std::string threadName;
        std::getline(comm, threadName);
        if (threadName == name) {
            return true;
        }
    }
    return false;
}

} // namespace

// Avahi never names its poll thread, so without this it shows up in ps -T and
// htop under the name of whatever thread started the publisher. avahi-daemon
// doesn't need to be running, since the publisher waits for one, but the system
// D-Bus does: without it the Avahi client can't be created at all, as in a bare
// container, so the test skips rather than fails there.
TEST(AvahiPublisher, NamesItsPollThread) {
    ASSERT_FALSE(threadNamed(AVAHI_POLL_THREAD_NAME));

    MdnsService service{"thread-name-test", "_creature-test._udp", 9, {}};
    AvahiPublisher publisher(std::make_shared<QuietLogger>(), service);
    if (!publisher.start()) {
        GTEST_SKIP() << "no system D-Bus, so no Avahi client and no poll thread to name";
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!threadNamed(AVAHI_POLL_THREAD_NAME) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(threadNamed(AVAHI_POLL_THREAD_NAME));

    publisher.stop();
}

} // namespace creatures::network
