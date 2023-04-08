#pragma once

#include "dnscache.hpp"
#include "dnsmessage.hpp"
#include "logger.hpp"
#include <netinet/in.h>
#include <array>
#include <memory>
#include <iostream>
#include <sstream>
#include <boost/asio/thread_pool.hpp>


inline constexpr int BUFF_SIZE = 512;
inline constexpr int FWD_SOCK_TIMEOUT = 5; // in sec

class Server
{
public:
    struct RequestData
    {
        int sockFD;
        const std::array<char, BUFF_SIZE> buffer;
        int size;
        const sockaddr_in clientAddr;
        const sockaddr_in forwardServerAddr;
    };

    Server(std::shared_ptr<DnsCache> cachePtr, int port, const sockaddr_in& fwdSrvAddr, const std::string& fwdAddrStr, int fwdPort);
    ~Server();

    void run();
    static int makeUdpSocket(const struct sockaddr_in& addr);

private:
    static void requestProcessor(RequestData data, std::shared_ptr<DnsCache> cache) noexcept;

    template<typename Msg>
    static void logMessage(const Msg& msg) noexcept
    {
        std::ostringstream ss;
        ss << msg;
        std::string logMsg(msg.getQr() == DNSHeader::Query ? "========Query info========" : "========Response info========");
        logMsg.append(ss.str());
        Logger::logDebug(logMsg);
        Logger::logToStdout(logMsg);
    }

    boost::asio::thread_pool thread_pool;  // make thread pool with size of available hardware concurrency x2
    std::shared_ptr<DnsCache> cache;
    sockaddr_in fwdServerAddr;
    struct sockaddr_in address;
    int socketFD;
};
