#include "SampledTexture.h"

#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/SamplerManager.h"

#include <vulkan/vulkan.hpp>

namespace Engine {

    struct SampledTexture::impl {
        SamplerDesc m_sampler_desc{};
        vk::Sampler m_sampler{};
    };

    SampledTexture::SampledTexture(RenderSystem &system) noexcept : Texture(system), pimpl(std::make_unique<impl>()) {
    }

    SampledTexture::~SampledTexture() = default;

    void SampledTexture::CreateTextureAndSampler(TextureDesc textureDesc, SamplerDesc samplerDesc, std::string name) {
        assert(
            (std::get<0>(ImageUtils::GetImageFlags(textureDesc.type)) & vk::ImageUsageFlagBits::eSampled)
            && "Sampled texture does not have sampled image usage flag."
        );
        Texture::CreateTexture(textureDesc, name);
        this->CreateSampler(samplerDesc);
    }

    void SampledTexture::CreateSampler(SamplerDesc samplerDesc) {
        pimpl->m_sampler = m_system.GetSamplerManager().GetSampler(samplerDesc);
        pimpl->m_sampler_desc = samplerDesc;
    }

    const SampledTexture::SamplerDesc &SampledTexture::GetSamplerDesc() const noexcept {
        return pimpl->m_sampler_desc;
    }

    vk::Sampler SampledTexture::GetSampler() const noexcept {
        assert(pimpl->m_sampler);
        return pimpl->m_sampler;
    }
} // namespace Engine
