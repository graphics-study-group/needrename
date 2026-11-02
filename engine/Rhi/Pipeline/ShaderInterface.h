#ifndef ENGINE_RHI_SHADERINTERFACE_INCLUDED
#define ENGINE_RHI_SHADERINTERFACE_INCLUDED

#include "Core/flagbits.h"
#include "Rhi/RhiTypes.h"
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace Engine::Rhi {
    class StructuredBufferPlacer;
    /**
     * @brief A shader parameter that occupies a descriptor slot.
     * Including opaque types and uniform or storage buffers.
     */
    struct SPInterface {
        /// @brief Name of the interface
        std::string name{};
        /// @brief Descriptor set specified by the layout
        uint32_t layout_set{~0U};
        /// @brief Descriptor binding specified by the layout
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
            CubeMap = 1 << 6,
            /// The image holds depth data and is sampled through a comparison sampler.
            Depth = 1 << 7
        };
        using ImageFlags = Flags<ImageFlagBits>;
        ImageFlags flags{};
    };

    /**
     * @brief A storage image interface.
     *
     * It carries nothing beyond the shared descriptor fields: the format a
     * storage image is declared with is a property of the texture the caller
     * binds, not of the interface, so there is nothing to record here.
     */
    struct SPInterfaceOpaqueStorageImage : SPInterfaceOpaque {
        /// @brief The format the shader declares the image with.
        ///
        /// A storage image's format is part of its declaration in GLSL, and a backend whose
        /// binding description names it cannot derive it from the texture the caller binds - the
        /// view must match what the shader says or the pipeline is rejected.
        uint32_t format{0};
        /// @brief Whether the shader only reads it (`readonly`).
        bool read_only{false};
    };

    /// @brief Other opaque interfaces such as samplers.
    struct SPInterfaceOpaqueOther : SPInterfaceOpaque {
        /// @brief Type of the interface.
        enum class Type {
            Unknown,
            // Separate sampler (`sampler`)
            Sampler
        } type{Type::Unknown};
    };

    /// @brief Buffers
    struct SPInterfaceBuffer : SPInterface {
        /// @brief Type of the interface.
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
} // namespace Engine::Rhi

#endif // ENGINE_RHI_SHADERINTERFACE_INCLUDED



