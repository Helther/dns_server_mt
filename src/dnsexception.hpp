#pragma once

#include <cstdint>
#include <string>
#include "dnsmessage.hpp"

class DNSException : public std::exception
{
public:
    DNSException(DNSHeader::RCode inCode, uint16_t msgId, const std::string& addInfo = std::string()) :
        code(inCode), id(msgId), info(addInfo) {}

    const char* what() const throw() override
    {
        static std::string result;
        switch (code)
        {
        case DNSHeader::Format:
             result = "Unable to interpret the query. ";
             break;
        case DNSHeader::NotImpl:
            result = "Query is not supported. ";
            break;
        case DNSHeader::NameError:
            result = "Domain Name doesn't exist. ";
            break;
        case DNSHeader::ServerFail:
            result = "Server Internal Error. ";
            break;
        default:
            return "Unknown Exception.";
        }
        return result.append(" Message id: " + std::to_string(id) + ". " + info).c_str();
    }
    
    DNSHeader::RCode code;
    uint16_t id;
    std::string info;
};
