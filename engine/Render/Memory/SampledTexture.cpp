#include "SampledTexture.h"

#include "Render/RenderSystem.h"
#include "Render/DebugUtils.h"

namespace Engine {
    SampledTexture::SampledTexture(RenderSystem &system) noexcept : Texture(system)
    {
    }

    void SampledTexture::CreateTextureAndSampler(TextureDesc textureDesc, SamplerDesc samplerDesc, std::string name)
    {
        assert(
            (std::get<0>(ImageUtils::GetImageFlags(textureDesc.type)) & vk::ImageUsageFlagBits::eSampled) 
            && "Sampled texture does not have sampled image usage flag."
        );
        Texture::CreateTexture(textureDesc, name);
        this->CreateSampler(samplerDesc);
    }

    void SampledTexture::CreateSampler(SamplerDesc samplerDesc) {
        this->m_sampler = m_system.getDevice().createSamplerUnique(vk::SamplerCreateInfo{
            vk::SamplerCreateFlags{},
            vk::Filter::eNearest, vk::Filter::eNearest,
            vk::SamplerMipmapMode::eNearest,
            vk::SamplerAddressMode::eRepeat,
            vk::SamplerAddressMode::eRepeat,
            vk::SamplerAddressMode::eRepeat,
            0.0,
            false,
            0.0,
            false,
            vk::CompareOp::eNever,
            0, 0, vk::BorderColor::eFloatTransparentBlack,
            false,
            nullptr
        });
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), this->m_sampler.get(), "Temporary Sampler for SampledTexture");
        this->m_sampler_desc = samplerDesc;
    }

    const SampledTexture::SamplerDesc &SampledTexture::GetSamplerDesc() const noexcept
    {
        return this->m_sampler_desc;
    }

    vk::Sampler SampledTexture::GetSampler() const noexcept
    {
        assert(this->m_sampler);
        return this->m_sampler.get();
    }
}
