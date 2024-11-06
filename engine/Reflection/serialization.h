#ifndef REFLECTION_SERIALIZATION_INCLUDED
#define REFLECTION_SERIALIZATION_INCLUDED

#include <vector>
#include <nlohmann/json.hpp>

namespace Engine
{
    namespace Serialization
    {
        using Json = nlohmann::json;
        struct SerializedBuffer
        {
            Json json;
            std::vector<std::pair<std::string, std::shared_ptr<std::byte>>> buffers; // Identification string, buffer start pointer
        };

        template <typename T>
        void save(const T& value, SerializedBuffer& buffer)
        {
            throw std::runtime_error("No serialization function found for type");
        }

        template <typename T>
        void load(T& value, const SerializedBuffer& buffer)
        {
            throw std::runtime_error("No serialization function found for type");
        }
    }
}

#include "generated/generated_serialization.hpp"

#endif
