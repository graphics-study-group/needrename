#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETER_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETER_INCLUDED

#include <unordered_map>
#include <string>
#include <variant>

#include <fwd.hpp>

namespace Engine {
    class Texture;
    class DeviceBuffer;
    class StructuredBuffer;
    class ComputeBuffer;

    namespace ShdrRfl {
        struct ShaderParameters {
            struct impl;
            std::unique_ptr <impl> pimpl;

            ShaderParameters();
            ~ShaderParameters() noexcept;

            using InterfaceVariant = std::variant<
                    std::monostate,
                    std::reference_wrapper<const Texture>,
                    // Buffer, offset and size
                    std::tuple<std::reference_wrapper<const DeviceBuffer>, size_t, size_t>
                >;

            void Assign(const std::string & name, uint32_t) noexcept;
            void Assign(const std::string & name, float) noexcept;
            void Assign(const std::string & name, const glm::vec4 &) noexcept;
            void Assign(const std::string & name, const glm::mat4 &) noexcept;
    
            void Assign(const std::string & name, float (& v) [4]) noexcept;
            void Assign(const std::string & name, float (& v) [16]) noexcept;

            /**
             * @brief Assign a texture by its name.
             * Gives shared ownership of the buffer object.
             */
            void Assign(const std::string & name, const Texture & value) noexcept;

            /**
             * @brief Assign a buffer by its name.
             * Does not transfer ownership of the buffer object.
             */
            void Assign(
                const std::string & name, 
                const DeviceBuffer & buf, 
                size_t offset = 0ULL, 
                size_t size = 0ULL
            ) noexcept;

            const std::unordered_map <std::string, InterfaceVariant> & GetInterfaces() const noexcept;
            const StructuredBuffer & GetStructuredBuffer() const noexcept;
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETER_INCLUDED
