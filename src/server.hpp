#pragma once

#include "dnscache.hpp"
#include "dnsmessage.hpp"
#include "logger.hpp"
#include <exception>
#include <netinet/in.h>
#include <array>
#include <memory>
#include <iostream>
#include <sstream>
#include <arpa/inet.h>
#include <string>
#include <vector>


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
    // RequestLogger is used to log received request at the end of the processing
    struct RequestLogger
    {
        RequestLogger(const RequestData& data) :
        clientAddr(data.clientAddr), requestSize(data.size) {}
        ~RequestLogger()
        {
            try
            {
                char clientAddrStr[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &clientAddr.sin_addr, clientAddrStr, INET_ADDRSTRLEN);
                const std::string logMsg("DNS Server received request from " + std::string(clientAddrStr) + ", with size: " + std::to_string(requestSize));
                Logger::logInfo(logMsg);
                Logger::logToStdout(logMsg);
                for (const auto& task : pendingTasks)
                {
                    Logger::logTask(task);
                    Logger::logToStdout(task.msg);
                }
            } catch (std::exception& e) {
                Logger::logToStdout(std::string("RequestLogger Error: ") + e.what());
            }
        }

        void addLogTask(LogLevel level, const std::string& msg)
        {
            pendingTasks.push_back({level, msg, std::time(nullptr)});
        }

    private:
        std::vector<LogTask> pendingTasks;
        const sockaddr_in clientAddr;
        int requestSize;
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
        const std::string logMsg = getLogMessage(msg);
        Logger::logDebug(logMsg);
        Logger::logToStdout(logMsg);
    }

    template<typename Msg>
    static std::string getLogMessage(const Msg& msg) noexcept
    {
        std::ostringstream ss;
        ss << msg;
        std::string logMsg(msg.getQr() == DNSHeader::Query ? "========Query info========" : "========Response info========");
        return logMsg.append(ss.str());
    }

    std::shared_ptr<DnsCache> cache;
    sockaddr_in fwdServerAddr;
    struct sockaddr_in address;
    int socketFD;
};
