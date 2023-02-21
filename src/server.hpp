#pragma once

#include "dnscache.hpp"
#include "dnsmessage.hpp"
#include <netinet/in.h>
#include <array>
#include <memory>
#include <iostream>
#include <sstream>

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
    static void requestProccessor(RequestData data, std::shared_ptr<DnsCache> cache);

    template<typename Msg>
    static void logMessage(const Msg& msg) noexcept
    {
        std::string logResp;
        std::ostringstream ss(logResp);
        ss << msg;
        std::cout << (msg.getQr() == DNSHeader::Query ? "========Query info========" : "========Response info========") << ss.str();
    }

    std::shared_ptr<DnsCache> cache;
    sockaddr_in fwdServerAddr;
    struct sockaddr_in address;
    int socketFD;
};
