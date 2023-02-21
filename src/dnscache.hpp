#pragma once

#include <map>
#include <mutex>
#include <string>
#include <shared_mutex>
#include <fstream>

 // in sec
inline constexpr int TIMEOUT_TIME = 60;

struct DnsEntry
{
    bool isEmpty() const noexcept
    {
        return address.empty();
    }

    std::string address;
    uint64_t lastUpdated;
};


class DnsCache
{
    std::map<std::string, DnsEntry> cache;
    mutable std::shared_mutex sharedMutex;
    std::fstream cacheFile;
    std::string cacheFileName;

public:
    DnsCache(const std::string &cacheFileName);
    DnsCache(const DnsCache&) = delete;
    ~DnsCache();

    // thread-safe non-blocking read access to cache
    DnsEntry lookupEntry(const std::string& name) const noexcept;
    // thread-safe write access
    void updateOrInsertEntry(const std::string& name, const DnsEntry& entry) noexcept;
    // in milliseconds
    static uint64_t getCurrentTimestamp() noexcept;
    void saveCacheToFile();

    const char entrySeparator= ' ';
};
