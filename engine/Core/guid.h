#ifndef CORE_GUID_INCLUDED
#define CORE_GUID_INCLUDED

#include <cstdint>
#include <string>
#include <sstream>
#include <random>
#include <iomanip>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class REFL_SER_CLASS(REFL_WHITELIST) GUID
    {
        REFL_SER_BODY()
    public:
        unsigned long long mostSigBits = 0;
        unsigned long long leastSigBits = 0;

        REFL_ENABLE GUID() = default;
        REFL_ENABLE GUID(const std::string &str);
        virtual ~GUID() = default;

        bool operator==(const GUID& other) const;

        REFL_ENABLE std::string toString() const;
        REFL_ENABLE void fromString(const std::string &str);

        void save_to_archive(Serialization::Archive& archive) const;
        void load_from_archive(Serialization::Archive& archive);
    };

    struct GUIDHash
    {
        std::size_t operator()(const GUID& guid) const
        {
            std::size_t h1 = std::hash<unsigned long long>{}(guid.mostSigBits);
            std::size_t h2 = std::hash<unsigned long long>{}(guid.leastSigBits);
            return h1 ^ (h2 << 1);
        }
    };
    
    template <typename Generator>
    GUID generateGUID(Generator& gen)
    {
        std::uniform_int_distribution<unsigned long long> dis(0, 0xFFFFFFFFFFFFFFFF);
        GUID guid;
        guid.mostSigBits = dis(gen);
        guid.leastSigBits = dis(gen);
        return guid;
    }
}

#endif // CORE_GUID_INCLUDED
