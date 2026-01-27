#ifndef RENDER_MEMORY_TEXTURE_INCLUDED
#define RENDER_MEMORY_TEXTURE_INCLUDED

#include "Render/ImageUtils.h"
#include "Render/Memory/TextureSlice.h"
#include <memory>

namespace vk {
    class Image;
    class ImageView;
    class Sampler;
} // namespace vk

namespace Engine {
    class RenderSystem;
    class AllocatedMemory;
    class DeviceBuffer;

    namespace RenderSystemState {
        class AllocatorState;
    };

    /**
     *  @brief A base class for textures with handles to 
     * allocated GPU resources. You should call named constructors
     * of its derived classes to obtain an instance.
     * 
     * @note Movable but non-copyable.
     */
    class Texture {
    public:
        using TextureDesc = ImageUtils::TextureDesc;
        using SamplerDesc = ImageUtils::SamplerDesc;

    protected:

        struct impl;
        std::unique_ptr <impl> pimpl;
        
        // Used in move operator for copy-and-swap
        Texture ();

        Texture(
            RenderSystem & system,
            TextureDesc texture,
            SamplerDesc sampler,
            const std::string & name = ""
        );

    public:

        Texture (const Texture &) = delete;
        void operator= (const Texture &) = delete;

        Texture (Texture &&) noexcept;
        // I don't want to rewrite the whole reference mess, so just delete it.
        Texture & operator = (Texture &&) noexcept = delete;
        
        virtual ~Texture();
        /**
         * @brief Get the description struct of this texture.
         */
        const TextureDesc & GetTextureDescription() const noexcept;

        /**
         * @brief Get the description struct of the sampler of this texture.
         */
        const SamplerDesc & GetSamplerDescription() const noexcept;

        /**
         * @brief Get the underlying handle of this texture.
         */
        vk::Image GetImage() const noexcept;

        /**
         * @brief Get the underlying handle of the sampler.
         */
        vk::Sampler GetSampler() const noexcept;

        /**
         * @brief Get a slice referring to the whole range
         * of its subresources (i.e. mipmaps and array layers)
         */
        const SlicedTextureView &GetFullSlice() const noexcept;

        /**
         * @brief Get the underlying handle of the full texture slice.
         */
        vk::ImageView GetImageView() const noexcept;

        /**
         * @brief Acquire a buffer large enough to hold the whole texture.
         */
        std::unique_ptr <DeviceBuffer> CreateStagingBuffer(const RenderSystemState::AllocatorState & allocator) const;

        /**
         * @brief Whether this texture supports random access (UAV
         * for HLSL, storage image for GLSL).
         */
        virtual bool SupportRandomAccess() const noexcept;

        /**
         * @brief Whether this texture supports atomic ops.
         */
        virtual bool SupportAtomicOperation() const noexcept;
    };
} // namespace Engine

#endif // RENDER_MEMORY_TEXTURE_INCLUDED
