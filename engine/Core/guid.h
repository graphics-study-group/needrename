#ifndef CORE_GUID_INCLUDED
#define CORE_GUID_INCLUDED

#include <Reflection/macros.h>
#include <cstdint>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace Engine {
    class REFL_SER_CLASS(REFL_WHITELIST) GUID {
        REFL_SER_BODY(GUID)
    public:
        uint64_t mostSigBits = 0;
        uint64_t leastSigBits = 0;

        REFL_ENABLE GUID() = default;
        REFL_ENABLE GUID(const std::string &str);
        virtual ~GUID() = default;

        bool operator==(const GUID &other) const;

        REFL_ENABLE std::string toString() const;
        REFL_ENABLE void fromString(const std::string &str);

        void save_to_archive(Serialization::Archive &archive) const;
        void load_from_archive(Serialization::Archive &archive);
    };

    template <typename Generator>
    GUID generateGUID(Generator &gen) {
        std::uniform_int_distribution<uint64_t> dis(0, 0xFFFFFFFFFFFFFFFF);
        GUID guid;
        guid.mostSigBits = dis(gen);
        guid.leastSigBits = dis(gen);
        return guid;
    }
} // namespace Engine

namespace std {
    template <>
    struct hash<Engine::GUID> {
        size_t operator()(const Engine::GUID &p) const noexcept {
            std::size_t h1 = std::hash<uint64_t>{}(p.mostSigBits);
            std::size_t h2 = std::hash<uint64_t>{}(p.leastSigBits);
            return h1 ^ (h2 << 1);
        }
    };
}

#endif // CORE_GUID_INCLUDED
