#include "server.hpp"
#include "dnsexception.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <cstring>
#include <arpa/inet.h>


Server::Server(std::shared_ptr<DnsCache> cachePtr, int port, const sockaddr_in &fwdSrvAddr, const std::string &fwdAddrStr, int fwdPort) :
    cache(cachePtr), fwdServerAddr(fwdSrvAddr)
{
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);
    socketFD = makeUdpSocket(address);


    std::cout << "DNS Server is initialized. Listening on port: " << port << " sockFD: " << socketFD << std::endl
        << "Forward server: ip: " << fwdAddrStr << " port: " << fwdPort << std::endl;
}

Server::~Server()
{
    close(socketFD);
    std::cout << "DNS Server shutdown\n";
}

void Server::run()
{
    std::cout << "DNS Server is running\n";

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof (clientAddr);
    char buffer[BUFF_SIZE];
    while(true)
    {
        int requestSize = recvfrom(socketFD, buffer, BUFF_SIZE, 0, (struct sockaddr*) &clientAddr, &clientAddrLen);
        char clientAddrStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientAddrStr, INET_ADDRSTRLEN);
        std::cout << "DNS Server received request from " << clientAddrStr << ", with size: " << requestSize << std::endl;
        std::array<char, BUFF_SIZE> arr;
        std::copy(buffer, buffer + BUFF_SIZE, arr.data());
        std::thread(requestProccessor, RequestData{socketFD, arr, requestSize, clientAddr, fwdServerAddr}, cache).detach();
        std::memset(buffer, 0, BUFF_SIZE);
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

void Server::requestProccessor(Server::RequestData data, std::shared_ptr<DnsCache> cache)
{
    try {
        DNSQuery query(data.buffer.data(), data.size);

        logMessage<DNSQuery>(query);

        DnsEntry entry = cache->lookupEntry(query.getData().qName);
        uint64_t currentTime = DnsCache::getCurrentTimestamp();
        char responseBuffer[BUFF_SIZE];
        int bytesWritten = 0;
        if (entry.isEmpty() || ((currentTime - entry.lastUpdated > TIMEOUT_TIME) && !entry.preloaded))
        {  // if not found in cache or cache entry time-outed and is not preloaded from file
            // create socket for forward server and send the request
            std::cout << "RequestProccessor get entry from Forward Server" << std::endl;
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

            // update cache with one answer
            const auto newData = fwdResponse.getData();
            if (newData.rData.empty())
                throw DNSException(DNSHeader::ServerFail, query.getId(), "Invalid response from Forward Server");

            cache->updateOrInsertEntry(newData.name, DnsEntry{newData.rData.front(), currentTime, false});
        }
        else
        {  // send entry directly from cache
            std::cout << "RequestProccessor get entry from cache" << std::endl;
            auto response = DNSResponse(DNSHeader::RCode::NoError, query, entry);
            bytesWritten = response.write(responseBuffer);

            logMessage<DNSResponse>(response);
        }
        int result = sendto(data.sockFD, responseBuffer, bytesWritten, 0, (struct sockaddr*) &data.clientAddr, sizeof(data.clientAddr));
        if (result == -1)
            throw std::runtime_error("Failed to send response to client.");

    } catch (DNSException& e) {
        std::cout << "RequestProccessor Caught DNS Exception: " << e.what() << std::endl;
        auto response = DNSResponse(e.code, e.id);
        char responseBuffer[BUFF_SIZE];
        int bytesWritten = response.write(responseBuffer);
        int result = sendto(data.sockFD, responseBuffer, bytesWritten, 0, (struct sockaddr*) &data.clientAddr, sizeof(data.clientAddr));
        if (result == -1)
            throw std::runtime_error("Failed to send error response to client.");
    } catch (std::exception& e) {
        std::cout << "RequestProccessor Caught Unhandled Exception: " << e.what() << std::endl;
    }
}
