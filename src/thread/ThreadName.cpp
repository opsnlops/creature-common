#include "creature/thread/ThreadName.h"

#if defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

namespace creatures {

std::string fitThreadName(const std::string &name) { return name.substr(0, MAX_THREAD_NAME_LENGTH); }

void setThreadName(const std::string &name) {
    const std::string fitted = fitThreadName(name);
#if defined(__linux__)
    pthread_setname_np(pthread_self(), fitted.c_str());
#elif defined(__APPLE__)
    // macOS can only name the calling thread
    pthread_setname_np(fitted.c_str());
#else
#warning "Threads won't be named, I don't know what platform this is"
    (void)fitted;
#endif
}

} // namespace creatures
