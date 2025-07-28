#include "guid.h"
#include <Reflection/serialization.h>

namespace Engine {
    GUID::GUID(const std::string &str) {
        mostSigBits = std::stoull(str.substr(0, 16), nullptr, 16);
        leastSigBits = std::stoull(str.substr(16, 16), nullptr, 16);
    }

    bool GUID::operator==(const GUID &other) const {
        return mostSigBits == other.mostSigBits && leastSigBits == other.leastSigBits;
    }

    std::string GUID::toString() const {
        std::stringstream ss;
        ss << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << mostSigBits;
        ss << std::hex << std::uppercase << std::setw(16) << std::setfill('0') << leastSigBits;
        return ss.str();
    }

    void GUID::fromString(const std::string &str) {
        mostSigBits = std::stoull(str.substr(0, 16), nullptr, 16);
        leastSigBits = std::stoull(str.substr(16, 16), nullptr, 16);
    }

    void GUID::save_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        json = toString();
    }

    void GUID::load_from_archive(Serialization::Archive &archive) {
        Serialization::Json &json = *archive.m_cursor;
        fromString(json.get<std::string>());
    }
} // namespace Engine
