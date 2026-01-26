#ifndef ENGINE_RENDER_IMAGEUTILS_INCLUDED
#define ENGINE_RENDER_IMAGEUTILS_INCLUDED

#include "Render/Memory/MemoryTypes.h"
#include <Reflection/macros.h>

namespace Engine {
    namespace ImageUtils {
        enum class REFL_SER_CLASS() ImageFormat {
            UNDEFINED,
            R8G8B8A8SNorm,
            R8G8B8A8UNorm,
            R8G8B8A8SRGB,
            // 32-bit packed float for HDR rendering. In Vulkan, it is actually B10-G11-R11.
            R11G11B10UFloat,
            R32G32B32A32SFloat,
            D32SFLOAT,
        };

        struct TextureDesc {
            // An integer between 1 and 3 for dimension.
            uint32_t dimensions;
            // Integers at least 1 for sizes.
            uint32_t width, height, depth;
            // Format of this image
            ImageFormat format;
            // Allowed memory access on this image.
            ImageMemoryType memory_type;
            // Use zero to automatically determine mipmap levels
            // by `max(log(width), log(height), log(depth))`
            uint32_t mipmap_levels;
            uint32_t array_layers;
            bool is_cube_map;
        };

        struct SamplerDesc {
            enum class REFL_SER_CLASS() AddressMode : uint8_t {
                Repeat,
                MirroredRepeat,
                ClampToEdge
            };

            enum class REFL_SER_CLASS() FilterMode : uint8_t {
                Point,
                Linear
            };

            // Filter mode.
            FilterMode min_filter{FilterMode::Point}, max_filter{FilterMode::Point}, mipmap_filter{FilterMode::Point};
            
            // Address mode on three axis
            AddressMode u_address{AddressMode::Repeat}, v_address{AddressMode::Repeat}, w_address{AddressMode::Repeat};

            // Bias and clamps on LoD (mipmaps).
            float bias_lod{0.0f}, min_lod{0.0f}, max_lod{0.0f};

            // Anisotropy value clamp. Use any value less than 1.0 to disable anisotropic filtering.
            float max_anisotropy{0.0f};

            // Declare a default elementwise equality operator
            bool operator== (const SamplerDesc&) const = default;

            template <bool use_float_hash = false>
            struct Hasher {
                size_t operator() (const SamplerDesc & s) const noexcept {
                    size_t hash = static_cast<uint8_t>(s.min_filter);
                    hash = hash << 2 + static_cast<uint8_t>(s.max_filter);
                    hash = hash << 2 + static_cast<uint8_t>(s.mipmap_filter);
                    hash = hash << 2 + static_cast<uint8_t>(s.u_address);
                    hash = hash << 2 + static_cast<uint8_t>(s.v_address);
                    hash = hash << 2 + static_cast<uint8_t>(s.w_address);

                    if constexpr (use_float_hash) {
                        // Maybe we should not hash fp numbers...
                        hash_combine(hash, s.bias_lod);
                        hash_combine(hash, s.min_lod);
                        hash_combine(hash, s.max_lod);
                        hash_combine(hash, s.max_anisotropy < 1.0f ? 0.0f : s.max_anisotropy);
                    }

                    return hash;
                };

            private:
                // Stolen from Boost
                static void hash_combine(size_t& seed, float value) {
                    std::hash<float> hasher;
                    seed ^= hasher(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                }
            };
        };
    } // namespace ImageUtils
} // namespace Engine

#endif // ENGINE_RENDER_IMAGEUTILS_INCLUDED
