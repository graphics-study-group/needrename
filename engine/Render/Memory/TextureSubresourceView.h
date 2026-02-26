#ifndef RENDER_MEMORY_TEXTURESUBRESOURCEVIEW
#define RENDER_MEMORY_TEXTURESUBRESOURCEVIEW

#include <cstdint>
#include <limits>

namespace vk {
    class ImageView;
}

namespace Engine {
    class Texture;

    /**
     * @brief Range of a subresource.
     * 
     * This subresource range, along with a reference to the parent texture,
     * identifies an image view (`VkImageView`).
     * 
     * @see `VkImageSubresourceRange`.
     */
    struct TextureSubresourceRange {
        uint32_t array_layer_base {0};
        /**
         * @brief How many array layers are visible in this view.
         * A special value `std::numeric_limits<uint32_t>::max()` specifies
         * all array layers.
         */
        uint32_t array_layer_size {std::numeric_limits<uint32_t>::max()};
        uint32_t mip_level_base {0};
        /**
         * @brief How mipmap layers are visible in this view.
         * A special value `std::numeric_limits<uint32_t>::max()` specifies
         * all mipmap levels.
         */
        uint32_t mip_level_size {std::numeric_limits<uint32_t>::max()};

        /// @brief Default equality operator provided by C++20.
        bool operator == (const TextureSubresourceRange & rhs) const = default;

        vk::ImageView GetImageView(Texture & t) const;

        /**
         * @brief Get a range that contains all array layers and all mip levels.
         */
        static constexpr TextureSubresourceRange GetFullRange() {
            return TextureSubresourceRange{};
        }

        /**
         * @brief Get a range that contains only the first array layer and
         * the first mip level.
         */
        static constexpr TextureSubresourceRange GetSingleRange() {
            return TextureSubresourceRange{0, 1, 0, 1};
        }
    };

    /**
     * @brief A subresource view of a given texture.
     * 
     * An indirection of Vulkan `VkImageView`.
     * 
     * The underlying `VkImageView` is lazily allocated, and is managed by the
     * parent texture.
     */
    class TextureSubresourceView {
    public:
        Texture & texture;
        TextureSubresourceRange range;

        /**
         * @brief Get the underlying image view.
         * 
         * Performs allocation if the given view is not created yet.
         */
        vk::ImageView GetImageView();
    };
};

#endif // RENDER_MEMORY_TEXTURESUBRESOURCEVIEW
