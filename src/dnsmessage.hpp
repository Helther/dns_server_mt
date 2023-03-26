#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <array>
#include <vector>
#include "dnscache.hpp"


struct DNSHeader
{
    enum QR
    {
        Query = 0,
        Response
    };

    enum OPCode
    {
        Standard = 0,
        Reverse,
        Status
    };

    enum RCode
    {
        NoError = 0,
        Format,
        ServerFail,
        NameError,
        NotImpl,
        Refused
    };

    uint16_t id = 0;
    uint16_t qr:1,
            opcode:4,
            aa:1,
            tc:1,
            rd:1,
            ra:1,
            z:3,
            rcode:4;
    uint16_t qdcount = 0;
    uint16_t ancount = 0;
    uint16_t nscount = 0;
    uint16_t arcount = 0;

    static constexpr uint16_t mask_qr = 0x8000;
    static constexpr uint16_t mask_opcode = 0x7800;
    static constexpr uint16_t mask_aa = 0x0400;
    static constexpr uint16_t mask_tc = 0x0200;
    static constexpr uint16_t mask_rd = 0x0100;
    static constexpr uint16_t mask_ra = 0x0080;
    static constexpr uint16_t mask_z = 0x0070;
    static constexpr uint16_t mask_rcode = 0x000F;

    static constexpr uint16_t headerOffset = 12;

    DNSHeader() noexcept
    {
        qr = 0;
        opcode = 0;
        aa = 0;
        tc = 0;
        rd = 0;
        ra = 0;
        z = 0;
    }
};

struct QueryData
{
    std::string qName;
    uint16_t qType;
    uint16_t qClass;
};

struct ResponseData
{
    std::string name;
    uint16_t type;
    uint16_t dataClass;
    uint32_t ttl;
    uint16_t rLength;
    std::vector<std::string> rData;
};


class DNSMessage
{
public:
    virtual ~DNSMessage(){}

    std::string toString() const noexcept;
    uint16_t getId() const noexcept { return header.id; }
    DNSHeader::QR getQr() const noexcept { return static_cast<DNSHeader::QR>(header.qr); }

protected:
    DNSMessage(){}

    void readHeader(const char* buffer);
    void writeHeader(char* buffer) const;

    uint16_t read16Bits(const char*& buffer) const;
    void write16Bits(char*& buffer, uint16_t value) const;
    uint32_t read32Bits(const char*& buffer, bool reverse) const;
    void write32Bits(char*& buffer, uint32_t value, bool reverse) const;   
    void writeLabel(char*& buffer, const std::string& name) const;
    void writeIPString(char*& buffer, const std::string& address) const;
    // returns qName byte size
    int readLabel(const char*& buffer, std::string &name) const;
    // from the start of the msg
    static uint16_t createNameOffset(uint8_t offset) noexcept;

    DNSHeader header;
};

class DNSQuery : public DNSMessage
{
public:
    DNSQuery(const char* packet, int size);

    QueryData getData() const noexcept { return data; }
    int write(char* buffer);

    friend std::ostringstream& operator<<(std::ostringstream& os, const DNSQuery& query)
    {
        const QueryData data = query.getData();
        os << "\nDNS Message\n{\n ";
        os << query.toString();
        os << "\n\tQuestion\n\tQNAME: " << data.qName << std::endl;
        os << "\tQTYPE: " << data.qType << std::endl;
        os << "\tQCLASS: " << data.qClass << std::endl;
        os << "}";
        return os;
    }

private:
    bool isQueryCompatible() const;
    // A type or any
    std::array<uint16_t, 2> compatibleTypes{0x01, 0xFF};
    // IN class or any
    std::array<uint16_t, 2> compatibleClasses{0x01, 0xFF};


    QueryData data;
};

class DNSResponse : public DNSMessage
{
public:
    // create empty error response
    DNSResponse(DNSHeader::RCode rCode, uint16_t id);
    // create response from query with answer entry
    DNSResponse(DNSHeader::RCode rCode, const DNSQuery& query, const DnsEntry& entry);
    // read response msg from forward server
    DNSResponse(DNSHeader::RCode rCode, const char* packet, int size);
    // encode response msg to buffer
    int write(char* buffer) const;
    ResponseData getData() const noexcept { return data; }

    friend std::ostringstream& operator<<(std::ostringstream& os, const DNSResponse& resp)
    {
        const ResponseData data = resp.getData();
        os << "\nDNS Message\n{\n ";
        os << resp.toString();
        os << "\n\tResponse\n\tNAME: " << data.name << std::endl;
        os << "\tTYPE: " << data.type << std::endl;
        os << "\tCLASS: " << data.dataClass << std::endl;
        os << "\tTTL: " << data.ttl << std::endl;
        os << "\tRDLENGTH: " << data.rLength << std::endl;
        for (const auto& ans : data.rData)
            os << "\tRDATA: " << ans << std::endl;
        os << "}";
        return os;
    }

private:
    ResponseData data;
};
