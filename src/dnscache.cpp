#include "dnscache.hpp"
#include <chrono>
#include <iostream>

DnsCache::DnsCache(const std::string& cacheFileName)
{
    this->cacheFileName = cacheFileName;
    cacheFile.open(this->cacheFileName, std::ios::in);
    if (!cacheFile.is_open())
    {
        std::cout << std::endl << "DNS cache not found, creating new file: " << cacheFileName << std::endl;
        cacheFile.open(cacheFileName, std::ios::out);
        if (!cacheFile.is_open())
            throw std::runtime_error("failed to create dns cache file");
    }
    else
    {
        // read hosts table
        std::string line;
        uint64_t timestamp = getCurrentTimestamp();
        while (!cacheFile.eof())
        {
            std::getline(cacheFile, line);
            if (line.empty())
                continue;

            auto addrPos = line.find_first_of(entrySeparator);
            auto domainPos = line.find_last_of(entrySeparator) + 1;
            if (addrPos == std::string::npos || domainPos == std::string::npos)
            {
                cacheFile.close();
                throw std::runtime_error("failed to read dns cache file entry");
            }
            std::string addrStr = line.substr(0, addrPos);
            std::string domainStr = line.substr(domainPos, line.size());
            cache.emplace(domainStr, DnsEntry{addrStr, timestamp});
        }
    }
    cacheFile.close();
}

DnsCache::~DnsCache()
{
    // save cache to hosts file
    saveCacheToFile();
}

DnsEntry DnsCache::lookupEntry(const std::string &name) const noexcept
{
    std::shared_lock lk(sharedMutex);
    auto entryIt = cache.find(name);
    return entryIt == cache.end() ? DnsEntry() : entryIt->second;
}

void DnsCache::updateOrInsertEntry(const std::string &name,
                                   const DnsEntry &entry) noexcept
{
    std::lock_guard<std::shared_mutex> lk(sharedMutex);
    cache[name] = entry;
}

uint64_t DnsCache::getCurrentTimestamp() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void DnsCache::saveCacheToFile()
{
    cacheFile.open(cacheFileName, std::ios::out | std::ios::trunc);
    if (!cacheFile.is_open())
    {
        std::cout << std::endl << "DNS cache failed to write to file " << cacheFileName << std::endl; 
        return;
    }
    std::shared_lock lk(sharedMutex);
    for (const auto& entry : cache)
        cacheFile << entry.second.address << ' ' << entry.first << std::endl;
    lk.unlock();
    cacheFile.close();
    std::cout << std::endl << "DNS cache written to file " << cacheFileName << std::endl; 
}
