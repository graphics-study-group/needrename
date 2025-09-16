#include "PipelineInfo.h"

#include <SDL3/SDL.h>
#include <cassert>
#include <glm.hpp>

#include "Render/Memory/Texture.h"
#include "Render/Memory/TextureSlice.h"

static std::pair<vk::ImageView, vk::Sampler> ExtractImageViewAndSampler(std::any var) noexcept {
    using namespace Engine;
    if (var.type() == typeid(std::shared_ptr<const Texture>)) {
        auto ptr = std::any_cast<std::shared_ptr<const Texture>>(var);
        return std::make_pair(ptr->GetImageView(), ptr->GetSampler());
    }
    if (var.type() == typeid(std::shared_ptr<const SlicedTextureView>)) {
        auto ptr = std::any_cast<std::shared_ptr<const SlicedTextureView>>(var);
        return std::make_pair(
            ptr->GetImageView(), ptr->GetTexture().GetSampler()
        );
    }

    // Or maybe we should force everyone to use immutable samplers...
    assert(!"A sampled image variable must be typed std::shared_ptr<const Texture> or std::shared_ptr<const "
            "SlicedTextureView>.");
    return std::make_pair(nullptr, nullptr);
}

static vk::ImageView ExtractImageViewForStorageImage(std::any var) noexcept {
    using namespace Engine;
    if (var.type() == typeid(std::shared_ptr<const Texture>)) {
        auto ptr = std::any_cast<std::shared_ptr<const Texture>>(var);
        return ptr->GetImageView();
    }
    if (var.type() == typeid(std::shared_ptr<const SlicedTextureView>)) {
        auto ptr = std::any_cast<std::shared_ptr<const SlicedTextureView>>(var);
        return ptr->GetImageView();
    }

    assert(!"A storage image variable must be typed std::shared_ptr<const Texture> or std::shared_ptr<const "
            "SlicedTextureView>.");
    return nullptr;
}

namespace Engine::PipelineInfo {
} // namespace Engine::PipelineInfo
