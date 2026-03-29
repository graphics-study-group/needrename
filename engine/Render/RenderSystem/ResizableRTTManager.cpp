#include "ResizableRTTManager.h"

#include <unordered_map>

namespace Engine::RenderSystemState {
    struct ResizableRTTManager::impl {

        struct Description {
            RenderTargetTexture::RenderTargetTextureDesc desc{};
            RenderTargetTexture::SamplerDesc sdesc {};
            float scale_x{.0f}, scale_y{.0f};
            std::string name{};
        };

        std::unordered_map <
            RRTTHandleEnum,
            Description
        > description_map {};

        std::unordered_map <
            RRTTHandleEnum,
            std::unique_ptr <RenderTargetTexture>
        > texture_map {};

        uint32_t monotonic_counter{0};
        uint32_t reference_width{0}, reference_height{0};

        /**
         * @brief Replace or create the RTT referred to by the iterator.
         */
        void ReplaceTexture(
            RenderSystem & system,
            decltype(description_map)::iterator itr
        ) {
            assert(reference_width > 0 && reference_height > 0);
            assert(itr != description_map.end());
            auto true_description = itr->second.desc;
            true_description.width = std::floor(reference_width * itr->second.scale_x);
            true_description.height = std::floor(reference_width * itr->second.scale_y);

            texture_map[itr->first] = RenderTargetTexture::CreateUnique(
                system,
                true_description,
                itr->second.sdesc,
                itr->second.name
            );
        }
    };

    ResizableRTTManager::ResizableRTTManager(
        RenderSystem &system
    ) : m_system(system), pimpl(std::make_unique<impl>()) {
    }
    ResizableRTTManager::~ResizableRTTManager() = default;

    RRTTHandle ResizableRTTManager::RequestRTT(
        RenderTargetTexture::RenderTargetTextureDesc description,
        RenderTargetTexture::SamplerDesc sampler_description,
        float width_factor,
        float height_factor,
        const std::string & name
    ) {
        pimpl->monotonic_counter++;

        auto handle = static_cast<RRTTHandleEnum>(pimpl->monotonic_counter);

        pimpl->description_map[handle] = {
            description,
            sampler_description,
            width_factor,
            height_factor,
            name
        };

        return RRTTHandle{*this, handle};
    }

    void ResizableRTTManager::SetReferenceSize(
        uint32_t width,
        uint32_t height
    ) noexcept {
        std::tie(pimpl->reference_width, pimpl->reference_height) = {width, height};
        this->RemoveAllCache();
    }

    void ResizableRTTManager::RemoveAllCache() noexcept {
        pimpl->texture_map.clear();
    }

    void ResizableRTTManager::RebuildCacheImmediately() {
        for (auto itr = pimpl->description_map.begin(); itr != pimpl->description_map.end(); ++itr) {
            pimpl->ReplaceTexture(m_system, itr);
        }
    }

    RenderTargetTexture &ResizableRTTManager::ResolveHandle(RRTTHandleEnum handle) {
        auto p = this->ResolveHandleToPtr(handle);
        if (!p) throw std::invalid_argument("Invalid handle");
        return *p;
    }

    RenderTargetTexture *ResizableRTTManager::ResolveHandleToPtr(RRTTHandleEnum handle) noexcept {
        const auto ditr = pimpl->description_map.find(handle);
        if (ditr == pimpl->description_map.end())   return nullptr;

        auto & tptr = pimpl->texture_map[handle];
        if (tptr) {
            return tptr.get();
        }

        pimpl->ReplaceTexture(m_system, ditr);
        return pimpl->texture_map[handle].get();
    }

    const RenderTargetTexture::RenderTargetTextureDesc &ResizableRTTManager::GetTextureDescription(
        RRTTHandleEnum handle
    ) const {
        auto itr = pimpl->description_map.find(handle);
        if (itr == pimpl->description_map.end())    throw std::invalid_argument("Invalid handle");
        return itr->second.desc;
    }

    std::unique_ptr<RenderTargetTexture> ResizableRTTManager::ReleaseRTT(RRTTHandleEnum handle) noexcept {
        auto itr = pimpl->texture_map.find(handle);
        auto ptr = std::move(itr->second);

        pimpl->texture_map.erase(handle);
        pimpl->description_map.erase(handle);

        return ptr;
    }

} // namespace Engine::RenderSystemState
