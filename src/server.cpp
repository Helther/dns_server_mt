#include "server.hpp"
#include "dnsexception.hpp"
#include "logger.hpp"
#include "threadpool.hpp"
#include <exception>
#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <cstring>
#include <sstream>
#include <functional>


Server::Server(DnsCache* cachePtr, int port, const sockaddr_in &fwdSrvAddr, const std::string &fwdAddrStr, int fwdPort) :
    cache(cachePtr), fwdServerAddr(fwdSrvAddr), threadPool(std::chrono::microseconds(THREAD_POOL_TASK_POLL_LATENCY))
{
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    socketFD = makeUdpSocket(address);

    std::ostringstream ss;
    ss << "DNS Server is initialized. Listening on port: " << port << " sockFD: " << socketFD
        << ". Forward server: ip: " << fwdAddrStr << " port: " << fwdPort;
    Logger::logInfo(ss.str());
    Logger::logToStdout(ss.str());
}

Server::~Server()
{
    close(socketFD);

    const std::string logMsg = "DNS Server shutdown";
    Logger::logInfo(logMsg);
    Logger::logToStdout(logMsg);
}

void Server::run()
{
    const std::string logMsg = "DNS Server is running";
    Logger::logInfo(logMsg);
    Logger::logToStdout(logMsg);

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof (clientAddr);
    char buffer[BUFF_SIZE];
    while(true)
    {
        try
        {
            int requestSize = recvfrom(socketFD, buffer, BUFF_SIZE, 0, (struct sockaddr*) &clientAddr, &clientAddrLen);
            std::array<char, BUFF_SIZE> arr;
            std::copy(buffer, buffer + BUFF_SIZE, arr.data());
            threadPool.submit(&requestProcessor, RequestData{socketFD, arr, requestSize, clientAddr, fwdServerAddr}, std::ref(*cache));
            std::memset(buffer, 0, BUFF_SIZE);
        } catch (std::exception& e) {
            const std::string logMsg(std::string("DNS Server Error receiving request") + e.what());
            Logger::logError(logMsg);
            Logger::logToStdout(logMsg);
        }
    }
}

int Server::makeUdpSocket(const sockaddr_in &addr)
{
    int socketFD;
    if ((socketFD = socket(AF_INET, SOCK_DGRAM, 0)) <= 0)
        throw std::runtime_error("Failed to create socket");

    if (bind(socketFD,(struct sockaddr*) &addr, sizeof (addr)) != 0)
    {
        close(socketFD);
        throw std::runtime_error("Failed to bind listen socket to address");
    }
    return socketFD;
}

void Server::requestProcessor(Server::RequestData data, DnsCache& cache) noexcept
{
    try {
        RequestLogger logRequest(data);  // log when out of scope
        DNSQuery query(data.buffer.data(), data.size);

        logRequest.addLogTask(LogLevel::DEBUG, getLogMessage(query));

        DnsEntry entry = cache.lookupEntry(query.getData().qName);
        uint64_t currentTime = DnsCache::getCurrentTimestamp();
        char responseBuffer[BUFF_SIZE];
        int bytesWritten = 0;
        if (entry.isEmpty() || ((currentTime - entry.lastUpdated > TIMEOUT_TIME) && !entry.preloaded))
        {  // if not found in cache or cache entry time-outed and is not preloaded from file
            // create socket for forward server and send the request
            logRequest.addLogTask(LogLevel::INFO, "RequestProccessor get entry from Forward Server");

            int fwdSock = socket(AF_INET, SOCK_DGRAM, 0);
            if (fwdSock <= 0)
                throw DNSException(DNSHeader::ServerFail, query.getId(), "Failed to create socket for Forward Server.");
            bytesWritten = query.write(responseBuffer);
            int resultBytes = sendto(fwdSock, responseBuffer, bytesWritten, 0, (struct sockaddr*) &data.forwardServerAddr, sizeof (data.forwardServerAddr));
            if (resultBytes == -1)
                throw DNSException(DNSHeader::ServerFail, query.getId(), "Failed to send query to Forward Server, consider restarting the server with another forward server.");

            struct timeval tv;
            tv.tv_sec = FWD_SOCK_TIMEOUT;
            tv.tv_usec = 0;
            if (setsockopt(fwdSock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
                throw DNSException(DNSHeader::ServerFail, query.getId(), "Failed to set Forward Server socket timeout");

            std::memset(responseBuffer, 0, BUFF_SIZE);
            socklen_t addrLen = sizeof (data.forwardServerAddr);
            resultBytes = recvfrom(fwdSock, responseBuffer, BUFF_SIZE, 0, (struct sockaddr *) &data.forwardServerAddr, &addrLen);
            if (resultBytes == -1)
                throw DNSException(DNSHeader::ServerFail, query.getId(), "Failed to get response from Forward Server, consider restarting the server with another forward server.");

            auto fwdResponse = DNSResponse(DNSHeader::RCode::NoError, responseBuffer, resultBytes);
            std::memset(responseBuffer, 0, BUFF_SIZE);
            bytesWritten = fwdResponse.write(responseBuffer);

            logMessage<DNSResponse>(fwdResponse);
            logRequest.addLogTask(LogLevel::DEBUG, getLogMessage(fwdResponse));
            // update cache with one answer
            const auto newData = fwdResponse.getData();
            if (newData.rData.empty())
                throw DNSException(DNSHeader::ServerFail, query.getId(), "Invalid response from Forward Server");

            cache.updateOrInsertEntry(newData.name, DnsEntry{newData.rData.front(), currentTime, false});
        }
        else
        {  // send entry directly from cache
            logRequest.addLogTask(LogLevel::INFO, "RequestProccessor get entry from cache");

            auto response = DNSResponse(DNSHeader::RCode::NoError, query, entry);
            bytesWritten = response.write(responseBuffer);

            logRequest.addLogTask(LogLevel::DEBUG, getLogMessage(response));
        }
        int result = sendto(data.sockFD, responseBuffer, bytesWritten, 0, (struct sockaddr*) &data.clientAddr, sizeof(data.clientAddr));
        if (result == -1)
            throw std::runtime_error("Failed to send response to client.");

    } catch (DNSException& e) {
        const std::string logMsg(std::string("RequestProccessor Caught DNS Exception: ") + e.what());
        Logger::logError(logMsg);
        Logger::logToStdout(logMsg);

        auto response = DNSResponse(e.code, e.id);
        char responseBuffer[BUFF_SIZE];
        int bytesWritten = response.write(responseBuffer);
        int result = sendto(data.sockFD, responseBuffer, bytesWritten, 0, (struct sockaddr*) &data.clientAddr, sizeof(data.clientAddr));
        if (result == -1)
        {
            const std::string logMsg(std::string("RequestProccessor Error sending error responce to client"));
            Logger::logError(logMsg);
            Logger::logToStdout(logMsg);
        }
    } catch (std::exception& e) {
        const std::string logMsg(std::string("RequestProccessor Caught Unhandled Exception: ") + e.what());
        Logger::logError(logMsg);
        Logger::logToStdout(logMsg);
    }
}
