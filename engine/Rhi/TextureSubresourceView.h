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
        /**
         * @brief Base array layer that is considered as the first layer for
         * this view.
         */
        uint32_t array_layer_base{0};
        /**
         * @brief How many array layers are visible in this view.
         * A special value `std::numeric_limits<uint32_t>::max()` specifies
         * all array layers.
         */
        uint32_t array_layer_size{std::numeric_limits<uint32_t>::max()};
        /**
         * @brief Base mipmap level that is considered as the first level for
         * this view.
         */
        uint32_t mip_level_base{0};
        /**
         * @brief How mipmap layers are visible in this view.
         * A special value `std::numeric_limits<uint32_t>::max()` specifies
         * all mipmap levels.
         */
        uint32_t mip_level_size{std::numeric_limits<uint32_t>::max()};

        /**
         * @brief Control swizzle and SRGB conversion of the image view.
         *
         * Swizzles are only supported if the image view is used for sampling.
         * SRGB conversions are limited to specific formats.
         */
        struct SwizzleAndSrgb {
            /**
             * @brief Controls color component swizzle.
             */
            enum class ColorSwizzle {
                Identity = 0, ///< Do not perform swizzle.
                Zero,         ///< Swizzle this channel to be zero.
                One,          ///< Swizzle this channel to be one.
                Red,          ///< Swizzle this channel to read from R channel (or depth).
                Green,        ///< Swizzle this channel to read from G channel.
                Blue,         ///< Swizzle this channel to read from B channel.
                Alpha         ///< Swizzle this channel to read from A channel.
            };

            /**
             * @brief Controls SRGB conversion.
             */
            enum class SrgbConversion {
                None = 0,  ///< Do not perform SRGB conversion.
                ForceSrgb, ///< Enforce SRGB gamma color encoding.
                ForceUnorm ///< Enfoce linear unsigned normalized FP encoding.
            };

            /// @brief Swizzle the red channel
            ColorSwizzle r : 4 {ColorSwizzle::Identity};
            /// @brief Swizzle the green channel
            ColorSwizzle g : 4 {ColorSwizzle::Identity};
            /// @brief Swizzle the blue channel
            ColorSwizzle b : 4 {ColorSwizzle::Identity};
            /// @brief Swizzle the alpha channel
            ColorSwizzle a : 4 {ColorSwizzle::Identity};
            /**
             * @brief Perform SRGB/UNorm conversion of the image view.
             *
             * This conversion is only performed if the format of the underlying
             * image can be converted between SRGB and Unorm, such as
             * `ImageUtils::ImageFormat::R8G8B8A8SRGB` and
             * `ImageUtils::ImageFormat::R8G8B8A8UNorm`
             */
            SrgbConversion srgb : 4 {SrgbConversion::None};

            bool operator==(const SwizzleAndSrgb &rhs) const = default;
        };
        static_assert(sizeof(SwizzleAndSrgb) <= sizeof(uint32_t));

        /**
         * @brief Controls color component swizzles and SRGB conversion of the
         * image view.
         *
         * This is an advanced feature of image view in testing. Bugs and
         * incomplete supports are expected.
         */
        SwizzleAndSrgb swizzle_and_srgb{};

        /// @brief Default equality operator provided by C++20.
        bool operator==(const TextureSubresourceRange &rhs) const = default;

        /**
         * @brief Acquire an image view for a texture.
         *
         * Performs creation if the given view is not created yet.
         */
        vk::ImageView GetImageView(Texture &t) const;

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
        Texture &texture;
        TextureSubresourceRange range;

        /**
         * @brief Get the underlying image view.
         *
         * Performs creation if the given view is not created yet.
         */
        vk::ImageView GetImageView();
    };
}; // namespace Engine

#endif // RENDER_MEMORY_TEXTURESUBRESOURCEVIEW
