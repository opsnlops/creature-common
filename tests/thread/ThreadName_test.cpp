#include <string>
#include <thread>

#include <gtest/gtest.h>

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

#include "creature/thread/ThreadName.h"

namespace creatures {

namespace {

#if defined(__linux__) || defined(__APPLE__)
// Name a fresh thread and read back what the OS recorded
std::string nameAThread(const std::string &name) {
    std::string recorded;
    std::thread([&] {
        setThreadName(name);
        char buffer[128] = {};
        pthread_getname_np(pthread_self(), buffer, sizeof(buffer));
        recorded = buffer;
    }).join();
    return recorded;
}
#endif

} // namespace

TEST(ThreadName, ShortNamesAreUnchanged) { EXPECT_EQ(fitThreadName("rtp-rx-3"), "rtp-rx-3"); }

TEST(ThreadName, LongNamesAreShortenedToThePlatformLimit) {
    const std::string name(MAX_THREAD_NAME_LENGTH + 20, 'x');
    EXPECT_EQ(fitThreadName(name), std::string(MAX_THREAD_NAME_LENGTH, 'x'));

    const std::string exact(MAX_THREAD_NAME_LENGTH, 'y');
    EXPECT_EQ(fitThreadName(exact), exact);
}

#if defined(__linux__) || defined(__APPLE__)
TEST(ThreadName, TheOperatingSystemRecordsTheName) { EXPECT_EQ(nameAThread("creature-test"), "creature-test"); }

// Without shortening, Linux rejects this name outright and the thread keeps
// its parent's name, which is what happened to most long names in both repos
TEST(ThreadName, ALongNameStillLabelsTheThread) {
    const std::string name = "creatures::server::ServerConnection::run-with-a-long-suffix-past-63";
    ASSERT_GT(name.size(), MAX_THREAD_NAME_LENGTH);
    EXPECT_EQ(nameAThread(name), name.substr(0, MAX_THREAD_NAME_LENGTH));
}
#endif

} // namespace creatures
