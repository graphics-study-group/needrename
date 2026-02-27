#include "RenderGraphBuilder2.h"

#include "Render/Pipeline/RenderGraph2/RenderGraph2.h"
#include "Render/Pipeline/RenderGraph2/RenderGraphPass.h"

namespace Engine {
    struct RenderGraphBuilder2::impl {
        std::vector <RenderGraphPass> passes{};

        // Imported and requested resources.
        struct ResourceStorage {
            int32_t resource_counter {0};

            struct TextureCreationInfo {
                std::string name{};
                RenderTargetTexture::RenderTargetTextureDesc t {};
                RenderTargetTexture::SamplerDesc s {};
            };
            std::unordered_map <int32_t, TextureCreationInfo> texture_creation_info;
            std::unordered_map <int32_t, const RenderTargetTexture *> texture_mapping;
            std::unordered_map <int32_t, const DeviceBuffer *> buffer_mapping;

            /**
             * @brief Materialize render target textures from the
             * `texture_creation_info`.
             */
            std::unordered_map <int32_t, std::unique_ptr<RenderTargetTexture>>
            MaterializeRenderTargetTextures(RenderSystem & s) const {
                std::unordered_map <int32_t, std::unique_ptr<RenderTargetTexture>> ret{};

                for (const auto & [k, v] : texture_creation_info) {
                    ret[k] = RenderTargetTexture::CreateUnique(
                        s,
                        v.t,
                        v.s,
                        v.name
                    );
                }

                return ret;
            }
        } rs{};
    };

    RenderGraphBuilder2::RenderGraphBuilder2(
        RenderSystem &system
    ) : system(system), pimpl(std::make_unique<impl>()) {
        // Append a source pass.
        this->AddPass(
            RenderGraphPassBuilder{system}.SetName("Virtual Source").Get()
        );
    }
    RenderGraphBuilder2::~RenderGraphBuilder2() = default;

    int32_t RenderGraphBuilder2::ImportExternalResource(
        const RenderTargetTexture &texture, MemoryAccessTypeImageBits prev_access
    ) {
        pimpl->rs.resource_counter++;
        auto ret = -pimpl->rs.resource_counter;
        pimpl->rs.texture_mapping[ret] = &texture;
        pimpl->passes.front().image_access[ret] = prev_access;
        return ret;
    }

    int32_t RenderGraphBuilder2::ImportExternalResource(
        const DeviceBuffer &buffer, MemoryAccessTypeBuffer prev_access
    ) {
        pimpl->rs.resource_counter++;
        auto ret = -pimpl->rs.resource_counter;
        pimpl->rs.buffer_mapping[ret] = &buffer;
        pimpl->passes.front().buffer_access[ret] = prev_access;
        return ret;
    }

    int32_t RenderGraphBuilder2::RequestRenderTargetTexture(
        RenderTargetTexture::RenderTargetTextureDesc texture_description,
        RenderTargetTexture::SamplerDesc sampler_description,
        std::string_view name
    ) noexcept {
        pimpl->rs.resource_counter++;
        auto ret = pimpl->rs.resource_counter;
        pimpl->rs.texture_creation_info[ret] = {
            .name = std::string{name},
            .t = texture_description,
            .s = sampler_description
        };
        return ret;
    }

    void RenderGraphBuilder2::AddPass(RenderGraphPass &&pass) noexcept {
        pimpl->passes.push_back(std::move(pass));
    }

    RenderGraph2 RenderGraphBuilder2::BuildRenderGraph() {
        return RenderGraph2();
    }

} // namespace Engine
