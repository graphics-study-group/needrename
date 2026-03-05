#ifndef ENGINE_RENDER_HASHER_INCLUDED
#define ENGINE_RENDER_HASHER_INCLUDED

#include <cstdint>

namespace Engine {
    template <typename T>
    concept HashableVulkanHppHandle = requires {
        typename T::CType;
        typename T::NativeType;
    };

    /**
     * @brief FNV hasher taken from VALVE fossilize lib
     */
    struct RenderResourceHasher {
        constexpr static size_t C = 0x100000001b3ull;
        constexpr static size_t S = 0xcbf29ce484222325ull;
        size_t s{S};

        template <typename T>
        inline void binary_data(const T *data, size_t size) noexcept {
            size /= sizeof(*data);
            for (size_t i = 0; i < size; i++)
                s = (s * C) ^ data[i];
        }

        inline void u32(uint32_t value) noexcept {
            s = (s * C) ^ value;
        }

        /**
         * @brief Hashing an enumeration.
         */
        template <typename T> requires std::is_enum_v<T>
        inline void e(T value) noexcept {
            static_assert(
                std::is_convertible_v<std::underlying_type_t<T>, uint32_t>
            );
            u32(static_cast<uint32_t>(value));
        }

        /**
         * @brief Hashing a Vulkan-Hpp flag.
         */
        template <typename T>
        inline void f(vk::Flags<T> value) noexcept {
            static_assert(
                std::is_convertible_v<typename vk::Flags<T>::MaskType, uint32_t>
            );
            u32(static_cast<vk::Flags<T>::MaskType>(value));
        }

        inline void s32(int32_t value) noexcept {
            u32(uint32_t(value));
        }

        inline void f32(float value) noexcept {
            u32(std::bit_cast<uint32_t>(value));
        }

        inline void u64(uint64_t value) noexcept {
            u32(value & 0xffffffffu);
            u32(value >> 32);
        }

        template <typename T>
        inline void pointer(T *ptr) {
            u64(std::bit_cast<uintptr_t>(ptr));
        }

        template <HashableVulkanHppHandle T>
        inline void handle(T h) {
            u64(
                std::bit_cast<uintptr_t>(
                    static_cast<typename T::CType>(h)
                )
            );
        }

        /**
         * @brief Convert any type by bit to `uint32_t` or `uint64_t` and hash
         * the converted integer.
         */
        template <typename T>
        inline void any(const T & a) {
            if constexpr (sizeof(T) == sizeof(uint32_t)) {
                u32(std::bit_cast<uint32_t>(a));
            } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
                u64(std::bit_cast<uint64_t>(a));
            } else {
                static_assert(false, "Failed to convert T to uint32_t or uint64_t");
            }
        }

        inline void string(const char *str) noexcept {
            char c;
            u32(0xff);
            while ((c = *str++) != '\0')
                u32(uint8_t(c));
        }

        inline void string(std::string_view str) noexcept {
            u32(0xff);
            for (auto &c : str)
                u32(uint8_t(c));
        }

        inline auto get() const {
            return s;
        }
    };
}

#endif // ENGINE_RENDER_HASHER_INCLUDED
