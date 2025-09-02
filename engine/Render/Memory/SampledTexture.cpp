#include "SampledTexture.h"

#include "Render/DebugUtils.h"
#include "Render/ImageUtilsFunc.h"
#include "Render/RenderSystem.h"

#include <vulkan/vulkan.hpp>

static constexpr vk::Filter ToVkFilter(Engine::SampledTexture::SamplerDesc::FilterMode filter) {
    using Mode = Engine::SampledTexture::SamplerDesc::FilterMode;
    switch (filter) {
    case Mode::Linear:
        return vk::Filter::eLinear;
    case Mode::Point:
        return vk::Filter::eNearest;
    }
    assert(false);
    return {};
}

static constexpr vk::SamplerMipmapMode ToVkSamplerMipmapMode(Engine::SampledTexture::SamplerDesc::FilterMode filter) {
    using Mode = Engine::SampledTexture::SamplerDesc::FilterMode;
    switch (filter) {
    case Mode::Linear:
        return vk::SamplerMipmapMode::eLinear;
    case Mode::Point:
        return vk::SamplerMipmapMode::eNearest;
    }
    assert(false);
    return {};
}

static constexpr vk::SamplerAddressMode ToVkSamplerAddressMode(Engine::SampledTexture::SamplerDesc::AddressMode addr) {
    using Mode = Engine::SampledTexture::SamplerDesc::AddressMode;
    switch (addr) {
    case Mode::Repeat:
        return vk::SamplerAddressMode::eRepeat;
    case Mode::MirroredRepeat:
        return vk::SamplerAddressMode::eMirroredRepeat;
    case Mode::ClampToEdge:
        return vk::SamplerAddressMode::eClampToEdge;
    }
    assert(false);
    return {};
}

namespace Engine {

    struct SampledTexture::impl {
        SamplerDesc m_sampler_desc{};
        // TODO: We need to allocate the sampler from a pool instead of creating it each time.
        vk::UniqueSampler m_sampler{};
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
        pimpl->m_sampler = m_system.getDevice().createSamplerUnique(
            vk::SamplerCreateInfo{
                vk::SamplerCreateFlags{},
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
                nullptr
            }
        );
        DEBUG_SET_NAME_TEMPLATE(m_system.getDevice(), pimpl->m_sampler.get(), "Temporary Sampler for SampledTexture");
        pimpl->m_sampler_desc = samplerDesc;
    }

    const SampledTexture::SamplerDesc &SampledTexture::GetSamplerDesc() const noexcept {
        return pimpl->m_sampler_desc;
    }

    vk::Sampler SampledTexture::GetSampler() const noexcept {
        assert(pimpl->m_sampler);
        return pimpl->m_sampler.get();
    }
} // namespace Engine
