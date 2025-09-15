#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETER_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETER_INCLUDED

#include <unordered_map>
#include <string>
#include <variant>

#include <fwd.hpp>

namespace Engine {
    class Texture;
    class Buffer;
    namespace ShdrRfl {
        struct ShaderParameters {
            using ParameterVariant = std::variant<
                    std::monostate,
                    uint32_t,
                    int32_t,
                    float,
                    glm::vec4,
                    glm::mat4
                >;
            using InterfaceVariant = std::variant<
                    std::monostate,
                    std::reference_wrapper<const Texture>,
                    std::vector <std::reference_wrapper<const Texture>>,
                    // Buffer, offset and size
                    std::tuple<std::reference_wrapper<const Buffer>, size_t, size_t>
                >;
            std::unordered_map <std::string, ParameterVariant> arguments;
            std::unordered_map <std::string, InterfaceVariant> interfaces;

            void Assign(const std::string & name, std::variant<uint32_t, int32_t, float> value) noexcept;

            void Assign(const std::string & name, std::variant<glm::vec4, glm::mat4> value) noexcept;

            void Assign(const std::string & name, const Texture & value) noexcept;

            void Assign(
                const std::string & name, 
                const Buffer & buf, 
                size_t offset = 0ULL, 
                size_t size = 0ULL
            ) noexcept;
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETER_INCLUDED
