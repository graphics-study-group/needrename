#ifndef MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED

#include <variant>
#include <cstdint>
#include <vector>
#include "Core/flagbits.h"

namespace Engine {
    class StructuredBufferPlacer;
    namespace ShdrRfl {
        /**
         * @brief A shader parameter that occupies a descriptor slot.
         * Including opaque types and uniform or storage buffers.
         */
        struct SPInterface {
            std::string name {};
            uint32_t layout_set {~0U};
            uint32_t layout_binding {~0U};

            virtual ~SPInterface() = default;
        };

        struct SPInterfaceOpaque : SPInterface {
            uint32_t array_size {0};
        };

        struct SPInterfaceOpaqueImage : SPInterfaceOpaque {
            enum class ImageFlagBits : uint32_t {
                HasSampler = 1 << 0,
                Arrayed = 1 << 1,
                Multisampled = 1 << 2,
                d1D = 1 << 3,
                d2D = 1 << 4,
                d3D = 1 << 5,
                CubeMap = 1 << 6
            };
            using ImageFlags = Flags <ImageFlagBits>;
            ImageFlags flags{};
        };

        struct SPInterfaceOpaqueStorageImage : SPInterfaceOpaque {
            // vk::Format format;
        };

        struct SPInterfaceOpaqueOther : SPInterfaceOpaque {
            enum class Type {
                Unknown,
                // Separate sampler (`sampler`)
                Sampler
            } type {Type::Unknown};
        };

        struct SPInterfaceBuffer : SPInterface {
            enum class Type {
                Unknown,
                // UBOs (`uniform StructName`)
                UniformBuffer,
                // SSBOs (`buffer StructName`)
                StorageBuffer
            } type {Type::Unknown};
        };

        struct SPInterfaceStructuredBuffer : SPInterfaceBuffer {
            const StructuredBufferPlacer * buffer_placer {nullptr};
        };
    }
}

#endif // MEMORY_SHADERPARAMETERS_SHADERPARAMETERINTERFACE_INCLUDED
