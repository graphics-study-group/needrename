#ifndef MEMORY_SHADERPARAMETERS_SHADERRESOURCEBINDING_INCLUDED
#define MEMORY_SHADERPARAMETERS_SHADERRESOURCEBINDING_INCLUDED

#include <memory>

// GLM forward declarations.
#include <fwd.hpp>

namespace Engine {

    class DeviceBuffer;
    class Texture;

    namespace RenderSystemState{
        class DeviceInterface;
    };

    /**
     * @brief A class that offers aggregated binding for shader resouces.
     * 
     * Includes:
     * - hash map mapping names to textures or buffers;
     * - descriptor set, cached to avoid additional writes.
     * 
     * As per Vulkan best practice, descriptor sets are not de-allocated. So
     * changes to descriptor sets (i.e. resetting texture or buffer references)
     * can lead to memory leak if too frequent.
     * 
     * Useing this class with `ShaderParameter` together is recommended.
     * The other class handles trivial uniform buffer variables (e.g. floats, 
     * vectors and matrics).
     */
    class ShaderResourceBinding {
        struct impl;
        std::unique_ptr <impl> pimpl;

    public:

        ShaderResourceBinding(
            RenderSystemState::ImmutableResourceCache & irc
        );
        ~ShaderResourceBinding() noexcept;

        /**
         * @brief Point an interface to a storage or uniform buffer.
         * 
         * @param offset Offset must meet the requirement specified by the
         * backend. For dynamic buffers, this offset is added to the dynamic
         * offset on binding time.
         */
        void BindBuffer(
            const std::string & name,
            const DeviceBuffer & buf,
            size_t offset = 0ULL, 
            size_t size = std::numeric_limits<size_t>::max()
        ) noexcept;

        /**
         * @brief Point an interface to a texture for sampling or random access.
         */
        void BindTexture(
            const std::string & name,
            const Texture & texture
        ) noexcept;

        /**
         * @brief Get the current descriptor set, determined by bound buffers
         * and textures, together with the layout.
         *
         * If no appropriate descriptor set is found, a new one will be
         * allocated from the pool supplied.
         */
        vk::DescriptorSet GetDescriptorSet(
            uint32_t set_id,
            const ShdrRfl::SPLayout & s,
            vk::Device d,
            vk::DescriptorPool pool,
            bool enforce_dynamic_uniform = false,
            bool enforce_dynamic_storage = false
        );
    };
}

#endif // MEMORY_SHADERPARAMETERS_SHADERRESOURCEBINDING_INCLUDED
