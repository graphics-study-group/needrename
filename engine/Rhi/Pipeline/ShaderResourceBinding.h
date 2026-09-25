#ifndef ENGINE_RHI_SHADERRESOURCEBINDING_INCLUDED
#define ENGINE_RHI_SHADERRESOURCEBINDING_INCLUDED

#include "Rhi/Texture/TextureSubresourceView.h"
#include "Rhi/rhi_export.h"

// GLM forward declarations.
#include <fwd.hpp>
#include <vulkan/vulkan.hpp>

#include <memory>

namespace Engine::Rhi {

    class DescriptorArena;
    class DeviceBuffer;
    class DeviceInterface;
    class ImmutableResourceCache;
    struct SPLayout;
    class Texture;

    /**
     * @brief A class that offers aggregated binding for shader resouces.
     *
     * Includes a hash map mapping names to textures or buffers, and maps those
     * names onto the reflected shader layout.
     *
     * As indicated by the interface, this class holds no ownership of
     * resources.
     *
     * The descriptor set itself is neither cached nor written here: the arena
     * keys, writes and keeps sets resident, and reclaims them once the
     * completed prefix has passed the epoch recorded for them. A caller must
     * therefore re-acquire a set in every epoch whose command buffers use it.
     *
     * Using this class with `StructuredBuffer` together is recommended.
     * The other class handles trivial uniform buffer variables (e.g. floats,
     * vectors and matrics).
     */
    class RHI_API ShaderResourceBinding {
        struct impl;
        std::unique_ptr<impl> pimpl;

    public:
        /// @param arena The device-scoped arena that owns the descriptor sets.
        explicit ShaderResourceBinding(Rhi::DescriptorArena &arena);
        ~ShaderResourceBinding() noexcept;

        /**
         * @brief Point an interface to a storage or uniform buffer.
         *
         * @param offset Offset must meet the requirement specified by the
         * backend. For dynamic buffers, this offset is added to the dynamic
         * offset on binding time.
         */
        void BindBuffer(
            const std::string &name,
            const DeviceBuffer &buf,
            size_t offset = 0ULL,
            size_t size = std::numeric_limits<size_t>::max()
        ) noexcept;

        /**
         * @brief Point an interface to a texture for sampling or random access.
         */
        void BindTexture(const std::string &name, Texture &texture) noexcept;

        /**
         * @brief Point an interface to a subresource of a texture.
         *
         * @note While you can operate on a subresource, it might not be
         * correctly sychronized, as currently the Render Graph operates on
         * resource granularity.
         */
        void BindTexture(const std::string &name, Texture &texture, TextureSubresourceRange range) noexcept;

        /**
         * @brief Get the descriptor set of a set index, determined by the bound
         * buffers and textures.
         *
         * The set is resolved and written by the arena on a miss, and reused
         * otherwise. Re-acquire it in every epoch whose command buffers use it.
         *
         * @param set_id The descriptor set index within the reflected layout.
         * @param layout The descriptor-set layout the caller resolved through the
         * arena and built its pipeline layout over.
         * @param s The reflected layout the set is built against.
         */
        vk::DescriptorSet GetDescriptorSet(
            uint32_t set_id,
            vk::DescriptorSetLayout layout,
            const Rhi::SPLayout &s,
            bool enforce_dynamic_uniform = false,
            bool enforce_dynamic_storage = false
        );
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_SHADERRESOURCEBINDING_INCLUDED
