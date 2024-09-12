#ifndef CORE_GUID_INCLUDED
#define CORE_GUID_INCLUDED

#include <cstdint>
#include <string>
#include <random>

namespace Engine
{
    struct GUID
    {
        uint64_t mostSigBits;
        uint64_t leastSigBits;

        bool operator==(const GUID& other) const
        {
            return mostSigBits == other.mostSigBits && leastSigBits == other.leastSigBits;
        }
    };

    struct GUIDHash
    {
        std::size_t operator()(const GUID& guid) const
        {
            std::size_t h1 = std::hash<uint64_t>{}(guid.mostSigBits);
            std::size_t h2 = std::hash<uint64_t>{}(guid.leastSigBits);
            return h1 ^ (h2 << 1);
        }
    };
    
    template <typename Generator>
    inline GUID generateGUID(Generator& gen)
    {
        std::uniform_int_distribution<uint64_t> dis(0, 0xFFFFFFFFFFFFFFFF);
        GUID guid;
        guid.mostSigBits = dis(gen);
        guid.leastSigBits = dis(gen);
        return guid;
    }

    inline std::string GUIDToString(const GUID &GUID)
    {
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << GUID.mostSigBits;
        ss << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << GUID.leastSigBits;
        return ss.str();
    }

    inline GUID stringToGUID(const std::string &str)
    {
        GUID GUID;
        GUID.mostSigBits = std::stoull(str.substr(0, 16), nullptr, 16);
        GUID.leastSigBits = std::stoull(str.substr(16, 16), nullptr, 16);
        return GUID;
    }
}

#endif // CORE_GUID_INCLUDED
