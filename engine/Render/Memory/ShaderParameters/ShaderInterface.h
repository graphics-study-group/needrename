#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED

#include "Core/flagbits.h"
#include <cstdint>
#include <variant>
#include <vector>

namespace Engine {
    class StructuredBufferPlacer;
    namespace ShdrRfl {
        /**
         * @brief A shader parameter that occupies a descriptor slot.
         * Including opaque types and uniform or storage buffers.
         */
        struct SPInterface {
            std::string name{};
            uint32_t layout_set{~0U};
            uint32_t layout_binding{~0U};

            virtual ~SPInterface() = default;
        };

        /// @brief Opaque types such as images or samplers.
        struct SPInterfaceOpaque : SPInterface {
            uint32_t array_size{0};
        };

        /// @brief Image type.
        struct SPInterfaceOpaqueImage : SPInterfaceOpaque {

            /// @brief Special attributes for images.
            enum class ImageFlagBits : uint32_t {
                HasSampler = 1 << 0,
                Arrayed = 1 << 1,
                Multisampled = 1 << 2,
                d1D = 1 << 3,
                d2D = 1 << 4,
                d3D = 1 << 5,
                CubeMap = 1 << 6
            };
            using ImageFlags = Flags<ImageFlagBits>;
            ImageFlags flags{};
        };

        struct SPInterfaceOpaqueStorageImage : SPInterfaceOpaque {
            // vk::Format format;
        };

        /// @brief Other opaque interfaces such as samplers.
        struct SPInterfaceOpaqueOther : SPInterfaceOpaque {
            enum class Type {
                Unknown,
                // Separate sampler (`sampler`)
                Sampler
            } type{Type::Unknown};
        };

        /// @brief Buffers
        struct SPInterfaceBuffer : SPInterface {
            enum class Type {
                Unknown,
                // UBOs (`uniform StructName`)
                UniformBuffer,
                // SSBOs (`buffer StructName`)
                StorageBuffer
            } type{Type::Unknown};
        };

        /// @brief Structured buffer. Generally uniform buffer for materials.   
        struct SPInterfaceStructuredBuffer : SPInterfaceBuffer {
            const StructuredBufferPlacer *buffer_placer{nullptr};
        };
    } // namespace ShdrRfl
} // namespace Engine

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
