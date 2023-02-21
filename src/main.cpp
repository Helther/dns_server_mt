#include "server.hpp"
#include <iostream>
#include <signal.h>
#include <memory>
#include <arpa/inet.h>
#include <array>

void checkPortValid(int port)
{
    if (port < 1 || port > 65535)
        throw std::runtime_error("Invalid port number");
}

static constexpr std::array<int, 6> SIGNALS_TO_INTERRUPT = {
    SIGABRT,
    SIGFPE,
    SIGILL,
    SIGINT,
    SIGSEGV,
    SIGTERM
};

std::shared_ptr<DnsCache> cache;

void handleServerInterrupt(int sig)
{
    std::weak_ptr<DnsCache> wptr = cache;
    if (auto sp = wptr.lock())
        if (sp->shouldSaveNewCacheFile())
            sp->saveCacheToFile();
    exit(sig);
}

void setupSigHandlers(void(*handler)(int))
{
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    for (const auto& sig : SIGNALS_TO_INTERRUPT)
        sigaction(sig, &sigIntHandler, NULL);
}


int main(int argc, char* argv[])
{
    setupSigHandlers(handleServerInterrupt);

    try
    {
        // parse args
        const std::string usage("Usage: dns_server port \"hosts_file_path\" \"forward_server_addr:fwd_srv_port\"(optional)\n");
        std::string hosts;
        std::string fwdAddr;
        int port, fwdPort;
        sockaddr_in fwdServerAddr;
        try {
            if (argc <= 2)
                throw std::runtime_error("Argument(s) missing");

            port = atoi(argv[1]);
            checkPortValid(port);

            hosts = argv[2];
            if (argc == 4)
            {
                std::string fwdStr(argv[3]);
                auto separatorPos = fwdStr.find(':');
                if (separatorPos == std::string::npos)
                    throw std::runtime_error("Invalid forward server address");

                fwdAddr = fwdStr.substr(0, separatorPos);

                fwdPort = atoi(fwdStr.substr(separatorPos + 1, fwdStr.size()).c_str());
            }
            else  // set default google DNS
            {
                fwdAddr = "8.8.8.8";
                fwdPort = 53;
            }
            fwdServerAddr.sin_family = AF_INET;
            if (inet_pton(AF_INET, fwdAddr.c_str(), &fwdServerAddr.sin_addr) != 1)
                throw std::runtime_error("Invalid forward server address");
            checkPortValid(fwdPort);

            fwdServerAddr.sin_port = htons(fwdPort);
        } catch (std::runtime_error e) {
            throw std::runtime_error("Invalid arguments. " + std::string(e.what()) + '\n' + usage);
        }

        // create cache
        cache = std::make_shared<DnsCache>(hosts);
        // start server
        Server dnsServer(cache, port, fwdServerAddr, fwdAddr, fwdPort);
        dnsServer.run();
    }
    catch (std::runtime_error& e)
    {
        std::cout << "Exception caught: " << e.what() << std::endl;
    }
    return 0;
}
