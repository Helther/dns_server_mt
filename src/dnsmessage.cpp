#include "dnsmessage.hpp"
#include <cstring>
#include <arpa/inet.h>
#include <iostream>
#include <algorithm>
#include "dnsexception.hpp"

std::string DNSMessage::toString() const noexcept
{
    std::ostringstream sstr;
    sstr << "\tID: "<< header.id << std::endl;
    sstr << "\t[ QR: " << header.qr << " opCode: " << header.opcode << " ]" << std::endl;
    sstr << "\tQDCOUNT: " << header.qdcount << std::endl;
    sstr << "\tANCOUNT: " << header.ancount << std::endl;
    sstr << "\tNSCOUNT: " << header.nscount << std::endl;
    sstr << "\tARCOUNT: " << header.arcount << std::endl;
    return sstr.str();
}

void DNSMessage::readHeader(const char *buffer)
{
    header.id = read16Bits(buffer);

    uint16_t bitField = read16Bits(buffer);
    header.qr = (bitField & header.mask_qr) >> 15;
    header.opcode = (bitField & header.mask_opcode) >> 14;
    header.aa = (bitField & header.mask_aa) >> 10;
    header.tc = (bitField & header.mask_tc) >> 9;
    header.rd = (bitField & header.mask_rd) >> 8;
    header.ra = (bitField & header.mask_ra) >> 7;
    // skip z flag
    header.rcode = bitField & header.mask_rcode;

    header.qdcount = read16Bits(buffer);
    header.ancount = read16Bits(buffer);
    header.nscount = read16Bits(buffer);
    header.arcount = read16Bits(buffer);
}

void DNSMessage::writeHeader(char* buffer) const
{
    write16Bits(buffer, header.id);

    int fields = (header.qr << 15);
    fields += (header.opcode << 14);
    fields += (header.aa << 10);
    fields += (header.tc << 9);
    fields += (header.rd << 8);
    fields += (header.ra << 7);
    // skip z flag
    fields += header.rcode;
    write16Bits(buffer, fields);

    write16Bits(buffer, header.qdcount);
    write16Bits(buffer, header.ancount);
    write16Bits(buffer, header.nscount);
    write16Bits(buffer, header.arcount);
}

uint16_t DNSMessage::read16Bits(const char*& buffer) const
{
    uint16_t result = static_cast<unsigned char>(buffer[0]);
    result <<= 8;
    result += static_cast<unsigned char>(buffer[1]);
    buffer += 2;
    return result;
}

void DNSMessage::write16Bits(char*& buffer, uint16_t value) const
{
    buffer[0] = (value & 0xFF00) >> 8;
    buffer[1] = value & 0xFF;
    buffer += 2;
}

uint32_t DNSMessage::read32Bits(const char *&buffer, bool reverse) const
{
    const std::array<unsigned char, 4> bytes{
        static_cast<unsigned char>(buffer[reverse ? 3 : 0]),
        static_cast<unsigned char>(buffer[reverse ? 2 : 1]),
        static_cast<unsigned char>(buffer[reverse ? 1 : 2]),
        static_cast<unsigned char>(buffer[reverse ? 0 : 3])
    };
    uint32_t result;
    std::memcpy(&result, bytes.data(), bytes.size());
    buffer += 4;
    return result;
}

void DNSMessage::write32Bits(char *&buffer, uint32_t value, bool reverse) const
{
    buffer[reverse ? 3 : 0] = (value & 0xFF000000) >> 24;
    buffer[reverse ? 2 : 1] = (value & 0xFF0000) >> 16;
    buffer[reverse ? 1 : 2] = (value & 0xFF00) >> 8;
    buffer[reverse ? 0 : 3] = (value & 0xFF) >> 0;
    buffer += 4;
}

void DNSMessage::writeLabel(char *&buffer, const std::string &name) const
{
    int start(0), end; // positions

    while ((end = name.find('.', start)) != std::string::npos) 
    {
        *buffer++ = end - start; // label length octet
        for (int i = start; i < end; i++) 
            *buffer++ = name[i]; // label octets
        start = end + 1; // Skip '.'
    }

    *buffer++ = name.size() - start; // last label length octet
    for (int i = start; i < name.size(); i++)
        *buffer++ = name[i]; // last label octets

    *buffer++ = 0;
}

void DNSMessage::writeIPString(char *&buffer, const std::string &address) const
{
    in_addr addr;
    inet_aton(address.c_str(), &addr);
    write32Bits(buffer, addr.s_addr, true);
}

int DNSMessage::readLabel(const char*& buffer, std::string &name) const
{
    int labelLength = *buffer++;
    int totalSize = labelLength + 1;
    while (labelLength != 0)  // scan domain until null terminated label
    {
        for (int i = 0; i < labelLength; i++)
        {
            char c = *buffer++;
            name.append(1, c);
        }
        labelLength = *buffer++;
        totalSize += labelLength + 1;
        if (labelLength != 0)
            name.append(1, '.');
    }

    return totalSize;
}

uint16_t DNSMessage::createNameOffset(uint8_t offset) noexcept
{
    uint16_t result = 0xc000;
    result += offset & 0xFF;
    return result;
}

DNSQuery::DNSQuery(const char* packet, int size)
{
    readHeader(packet);
    if (header.id == 0 || header.qr != DNSHeader::Query)
        throw DNSException(DNSHeader::Format, header.id);

    packet += DNSHeader::headerOffset;
    readLabel(packet, data.qName);
    data.qType = read16Bits(packet);
    data.qClass = read16Bits(packet);
    if (!isQueryCompatible())
        throw DNSException(DNSHeader::NotImpl, header.id);
}

int DNSQuery::write(char *buffer)
{
    char* begin = buffer;
    header.arcount = 0;
    writeHeader(buffer);
    buffer += DNSHeader::headerOffset;

    writeLabel(buffer, data.qName);
    write16Bits(buffer, data.qType);
    write16Bits(buffer, data.qClass);

    return buffer - begin;
}

bool DNSQuery::isQueryCompatible() const
{
    bool checkType = std::any_of(compatibleTypes.begin(), compatibleTypes.end(), 
    [this](uint16_t type){ return type == data.qType; });
    bool checkClass = std::any_of(compatibleClasses.begin(), compatibleClasses.end(), 
    [this](uint16_t qClass){ return qClass == data.qClass; });
    return header.qdcount == 1 && header.opcode == DNSHeader::Standard && checkType && checkClass;
}


DNSResponse::DNSResponse(DNSHeader::RCode rCode, uint16_t id)
{
    header.id = id;
    header.rcode = rCode;
    header.qr = DNSHeader::QR::Response;
}

DNSResponse::DNSResponse(DNSHeader::RCode rCode, const DNSQuery& query, const DnsEntry& entry)
{
    const auto queryData = query.getData();
    header.id = query.getId();
    header.rcode = rCode;
    header.qr = DNSHeader::QR::Response;
    header.qdcount = 1;
    header.ancount = 1;
    data.name = queryData.qName;
    data.type = queryData.qType;
    data.dataClass = queryData.qClass;
    data.ttl = TIMEOUT_TIME;
    data.rLength = 4;
    data.rData = entry.address;
}

DNSResponse::DNSResponse(DNSHeader::RCode rCode, const char *packet, int size)
{
    readHeader(packet);
    if (header.id == 0 || header.qr != DNSHeader::Response || header.rcode != DNSHeader::NoError || header.ancount == 0)
        throw DNSException(static_cast<DNSHeader::RCode>(header.rcode), header.id, "Invalid Response from Forward Server.");

    header.qr = DNSHeader::QR::Response;
    header.ra = 0;
    packet += DNSHeader::headerOffset;
    readLabel(packet, data.name);
    data.type = read16Bits(packet);
    data.dataClass = read16Bits(packet);
    // TODO parse  all answers
    packet += 2; // skip NAME offset
    packet += 10; // skip TYPE, CLASS, TTL, RDLENGTH bit fields
    data.ttl = 60;
    data.rLength = 4;
    in_addr addrN;
    addrN.s_addr = read32Bits(packet, false);
    char clientAddrStr[INET_ADDRSTRLEN];
    data.rData = inet_ntop(AF_INET, &addrN.s_addr, clientAddrStr, INET_ADDRSTRLEN);

    if (data.name.empty() || data.rData.empty())
        throw std::runtime_error("Failed to parse answer from Forward Server");
}

int DNSResponse::write(char* buffer) const
{
    char* begin = buffer;
    writeHeader(buffer);
    buffer += DNSHeader::headerOffset;
    if (!data.name.empty())
    {
        writeLabel(buffer, data.name);
        write16Bits(buffer, data.type);
        write16Bits(buffer, data.dataClass);
        // TODO write all answers
        write16Bits(buffer, createNameOffset(DNSHeader::headerOffset)); // offset to qName
        write16Bits(buffer, data.type);
        write16Bits(buffer, data.dataClass);
        write32Bits(buffer, data.ttl, false);
        write16Bits(buffer, data.rLength);
        writeIPString(buffer, data.rData);
    }

    return buffer - begin;
}
