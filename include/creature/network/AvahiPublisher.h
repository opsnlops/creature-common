#pragma once

#include <memory>
#include <string>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/thread-watch.h>

#include "creature/logging/Logger.h"
#include "creature/network/MdnsService.h"

namespace creatures::network {

/**
 * Build an Avahi TXT list that publishes the entries in the service's order
 *
 * The caller owns the list and frees it with avahi_string_list_free().
 */
AvahiStringList *makeAvahiTxt(const MdnsService &service);

/**
 * Advertises a service over mDNS through avahi-daemon for as long as it runs
 *
 * Advertising is a convenience, so nothing here stops the controller. If the daemon
 * isn't running, the publisher waits for it, and it re-registers if the daemon
 * restarts or another device already uses the name.
 */
class AvahiPublisher {
  public:
    AvahiPublisher(std::shared_ptr<Logger> logger, MdnsService service);
    ~AvahiPublisher();

    AvahiPublisher(const AvahiPublisher &) = delete;
    AvahiPublisher &operator=(const AvahiPublisher &) = delete;

    /**
     * Start advertising
     *
     * @return false if the publisher couldn't be set up at all
     */
    bool start();

    /**
     * Stop advertising and release everything
     */
    void stop();

    /**
     * Replace the advertised TXT records, such as once the firmware reports its version
     *
     * The name, type, and port stay as they were. Safe to call from any thread
     * except Avahi's own.
     */
    void updateTxt(std::vector<std::pair<std::string, std::string>> txt);

  private:
    static void clientCallback(AvahiClient *client, AvahiClientState state, void *userdata);
    static void groupCallback(AvahiEntryGroup *group, AvahiEntryGroupState state, void *userdata);

    // These run on Avahi's poll thread, apart from the first client callback
    void onClientState(AvahiClient *client, AvahiClientState state);
    void onGroupState(AvahiEntryGroup *group, AvahiEntryGroupState state);
    void createService(AvahiClient *client);
    void chooseAlternativeName();
    void fitRecord();
    bool connect();

    std::shared_ptr<Logger> logger;
    MdnsService service;
    std::string currentName;

    AvahiThreadedPoll *poll = nullptr;
    AvahiClient *client = nullptr;
    AvahiEntryGroup *group = nullptr;
    bool established = false;
};

} // namespace creatures::network
