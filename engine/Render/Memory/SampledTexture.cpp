#include "SampledTexture.h"

#include "Render/DebugUtils.h"
#include "Render/RenderSystem.h"

vk::Filter ToVkFilter(Engine::SampledTexture::SamplerDesc::FilterMode filter) {
    using Mode = Engine::SampledTexture::SamplerDesc::FilterMode;
    switch (filter) {
    case Mode::Linear:
        return vk::Filter::eLinear;
    case Mode::Point:
        return vk::Filter::eNearest;
    }
}

vk::SamplerMipmapMode ToVkSamplerMipmapMode(Engine::SampledTexture::SamplerDesc::FilterMode filter) {
    using Mode = Engine::SampledTexture::SamplerDesc::FilterMode;
    switch (filter) {
    case Mode::Linear:
        return vk::SamplerMipmapMode::eLinear;
    case Mode::Point:
        return vk::SamplerMipmapMode::eNearest;
    }
}

vk::SamplerAddressMode ToVkSamplerAddressMode(Engine::SampledTexture::SamplerDesc::AddressMode addr) {
    using Mode = Engine::SampledTexture::SamplerDesc::AddressMode;
    switch (addr) {
    case Mode::Repeat:
        return vk::SamplerAddressMode::eRepeat;
    case Mode::MirroredRepeat:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case Mode::ClampToEdge:
        return vk::SamplerAddressMode::eClampToEdge;
    }
}

namespace Engine {

    SampledTexture::SampledTexture(RenderSystem &system) noexcept : Texture(system) {
    }

    void SampledTexture::CreateTextureAndSampler(TextureDesc textureDesc, SamplerDesc samplerDesc, std::string name) {
        assert((std::get<0>(ImageUtils::GetImageFlags(textureDesc.type)) & vk::ImageUsageFlagBits::eSampled)
               && "Sampled texture does not have sampled image usage flag.");
        Texture::CreateTexture(textureDesc, name);
        this->CreateSampler(samplerDesc);
    }

    void SampledTexture::CreateSampler(SamplerDesc samplerDesc) {
        this->m_sampler = m_system.getDevice().createSamplerUnique(
            vk::SamplerCreateInfo{vk::SamplerCreateFlags{},
                                  ToVkFilter(samplerDesc.min_filter),
                                  ToVkFilter(samplerDesc.max_filter),
                                  ToVkSamplerMipmapMode(samplerDesc.mipmap_filter),
                                  ToVkSamplerAddressMode(samplerDesc.u_address),
                                  ToVkSamplerAddressMode(samplerDesc.v_address),
                                  ToVkSamplerAddressMode(samplerDesc.w_address),
                                  samplerDesc.biasLod,
                                  false,
                                  0.0,
                                  false,
                                  vk::CompareOp::eNever,
                                  samplerDesc.minLod,
                                  samplerDesc.maxLod,
                                  vk::BorderColor::eFloatTransparentBlack,
                                  false,
                                  nullptr});
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), this->m_sampler.get(), "Temporary Sampler for SampledTexture");
        this->m_sampler_desc = samplerDesc;
    }

    const SampledTexture::SamplerDesc &SampledTexture::GetSamplerDesc() const noexcept {
        return this->m_sampler_desc;
    }

    vk::Sampler SampledTexture::GetSampler() const noexcept {
        assert(this->m_sampler);
        return this->m_sampler.get();
    }
} // namespace Engine
