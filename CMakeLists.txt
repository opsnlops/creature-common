cmake_minimum_required(VERSION 3.25)

project(creature-common
        VERSION 0.1.0
        DESCRIPTION "Shared libraries for April's Creature Workshop"
        HOMEPAGE_URL https://github.com/opsnlops/creature-common
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Same warning set the consumers build with, so a warning never shows up for the
# first time in somebody else's build. Applied per target rather than globally,
# so fetched dependencies aren't judged by our standards.
set(CREATURE_COMMON_WARNINGS -Wshadow -Wall -Wextra -Wpedantic)

# Tests only when this is the top-level project. Consumers pull this in with
# FetchContent and must not inherit our test targets into their suites.
if(PROJECT_IS_TOP_LEVEL)
    set(CREATURE_COMMON_BUILD_TESTS_DEFAULT ON)
else()
    set(CREATURE_COMMON_BUILD_TESTS_DEFAULT OFF)
endif()
option(CREATURE_COMMON_BUILD_TESTS "Build the creature-common tests"
       ${CREATURE_COMMON_BUILD_TESTS_DEFAULT})

# Defer to the parent project's fmt when there is one. Both consumers already
# make fmt available, so declare creature-common *after* fmt in their
# CMakeLists; this fallback is for building standalone.
if(NOT TARGET fmt::fmt)
    include(FetchContent)
    FetchContent_Declare(
            fmt
            GIT_REPOSITORY https://github.com/fmtlib/fmt
            GIT_TAG 12.1.0
    )
    FetchContent_MakeAvailable(fmt)
endif()

# ---- creature::logging -------------------------------------------------------
# The Logger interface, so a library can log without knowing who is listening.
# Header-only; implementations live in the consumers (the controller's
# SpdlogLogger, the server's adapter onto its spdlog free functions).
add_library(creature_logging INTERFACE)
add_library(creature::logging ALIAS creature_logging)
target_include_directories(creature_logging INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(creature_logging INTERFACE fmt::fmt)

# ---- creature::mdns ----------------------------------------------------------
# DNS-SD advertising. Publishing goes through avahi-daemon, so the publisher is
# only built on Linux; elsewhere the record types still compile and nothing is
# advertised.
add_library(creature_mdns STATIC src/network/MdnsService.cpp)
add_library(creature::mdns ALIAS creature_mdns)
target_include_directories(creature_mdns PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(creature_mdns PUBLIC creature::logging)
target_compile_options(creature_mdns PRIVATE ${CREATURE_COMMON_WARNINGS})

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(AVAHI_CLIENT REQUIRED IMPORTED_TARGET avahi-client)
    target_sources(creature_mdns PRIVATE src/network/AvahiPublisher.cpp)
    target_link_libraries(creature_mdns PUBLIC PkgConfig::AVAHI_CLIENT PRIVATE creature::thread)
    target_compile_definitions(creature_mdns PUBLIC CREATURE_HAS_AVAHI=1)
endif()

# ---- creature::rtp -----------------------------------------------------------
# The RTP/RTCP wire contract: the constants both sides must agree on, and one
# encode/decode pair for the RTCP Sender Report. The sender used to own the
# encoder and the receivers the decoder, with no shared definition of the format
# between them (#1, #2).
add_library(creature_rtp STATIC
        src/rtp/Protocol.cpp
        src/rtp/RtcpPacket.cpp
)
add_library(creature::rtp ALIAS creature_rtp)
target_include_directories(creature_rtp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(creature_rtp PUBLIC fmt::fmt)
target_compile_options(creature_rtp PRIVATE ${CREATURE_COMMON_WARNINGS})

# ---- creature::thread --------------------------------------------------------
# Naming threads so they show up in htop and Instruments. Both consumers carried
# the same copy, with the same broken #elif (#4).
find_package(Threads REQUIRED)
add_library(creature_thread STATIC src/thread/ThreadName.cpp)
add_library(creature::thread ALIAS creature_thread)
target_include_directories(creature_thread PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(creature_thread PUBLIC Threads::Threads)
target_compile_options(creature_thread PRIVATE ${CREATURE_COMMON_WARNINGS})

# ---- creature::timesync ------------------------------------------------------
# PTP and system clock health, shared by the controllers and the server. The
# controllers are clients; the server is the grandmaster, and the evaluation is
# role-aware rather than duplicated per side (#3).
add_library(creature_timesync STATIC
        src/timesync/PtpManagement.cpp
        src/timesync/TimeSync.cpp
        src/timesync/TimeSyncMonitor.cpp
)
add_library(creature::timesync ALIAS creature_timesync)
target_include_directories(creature_timesync PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(creature_timesync PUBLIC creature::logging)
target_compile_options(creature_timesync PRIVATE ${CREATURE_COMMON_WARNINGS})

if(CREATURE_COMMON_BUILD_TESTS)
    include(CTest)
    include(FetchContent)
    FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(creature-common-test
            tests/network/MdnsService_test.cpp
            tests/rtp/Protocol_test.cpp
            tests/rtp/RtcpPacket_test.cpp
            tests/thread/ThreadName_test.cpp
            tests/timesync/PtpManagement_test.cpp
            tests/timesync/TimeSync_test.cpp
    )
    target_link_libraries(creature-common-test PRIVATE creature::mdns creature::rtp creature::thread creature::timesync gtest_main gmock)
    # Needs Avahi, so only on Linux, like the publisher itself
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_sources(creature-common-test PRIVATE tests/network/AvahiPublisher_test.cpp)
    endif()
    target_compile_options(creature-common-test PRIVATE ${CREATURE_COMMON_WARNINGS})

    include(GoogleTest)
    gtest_discover_tests(creature-common-test)
endif()
