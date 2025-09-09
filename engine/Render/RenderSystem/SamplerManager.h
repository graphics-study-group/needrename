#ifndef RENDER_RENDERSYSTEM_SAMPLERMANAGER_INCLUDED
#define RENDER_RENDERSYSTEM_SAMPLERMANAGER_INCLUDED

#include <memory>

namespace vk {
    class Sampler;
}

namespace Engine {
    class RenderSystem;

    struct SamplerDesc {
        enum class AddressMode : uint8_t {
            Repeat,
            MirroredRepeat,
            ClampToEdge
        };

        enum class FilterMode : uint8_t {
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

    namespace RenderSystemState {
        class SamplerManager {
            RenderSystem & m_system;
            struct impl;
            std::unique_ptr <impl> pimpl;
        public:

            SamplerManager(RenderSystem & system);
            ~SamplerManager();

            /**
             * @brief Get a sampler from the a sampler description.
             * If the sampler matching the current description does not exist,
             * it will be created. Samplers created and managed by this class
             * will not be destroyed until the deconstruction of this class.
             */
            vk::Sampler GetSampler(SamplerDesc desc);
        };
    }
}

#endif // RENDER_RENDERSYSTEM_SAMPLERMANAGER_INCLUDED
