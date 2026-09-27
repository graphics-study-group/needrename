#include "Rhi/Pipeline/ShaderResourceBinding.h"

#include "Rhi/Buffer/DeviceBuffer.h"
#include "Rhi/Pipeline/InterfaceBindingResolver.h"
#include "Rhi/Pipeline/ShaderInterface.h"
#include "Rhi/Pipeline/ShaderParameterLayout.h"
#include "Rhi/Resource/DescriptorArena.h"
#include "Rhi/Texture/Texture.h"

#include <cassert>
#include <map>
#include <tuple>
#include <variant>
#include <vector>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct ShaderResourceBinding::impl {
        Rhi::DescriptorArena *arena{nullptr};

        using InterfaceVariant = std::variant<
            std::monostate,
            // XXX: immutable sampler is not properly considered for hashing.
            std::tuple<vk::ImageView, vk::Sampler>,
            // Buffer, offset and size
            std::tuple<vk::Buffer, size_t, size_t>>;

        // This map has to be ordered to ensure consistent content.
        std::map<std::string, InterfaceVariant> interfaces{};
    };

    ShaderResourceBinding::ShaderResourceBinding(Rhi::DescriptorArena &arena) : pimpl(std::make_unique<impl>()) {
        pimpl->arena = &arena;
    }

    ShaderResourceBinding::~ShaderResourceBinding() noexcept = default;

    void ShaderResourceBinding::BindBuffer(
        const std::string &name, const DeviceBuffer &buf, size_t offset, size_t size
    ) noexcept {
        static_assert(vk::WholeSize == std::numeric_limits<size_t>::max());
        pimpl->interfaces[name] = std::make_tuple(buf.GetBuffer(), offset, size);
    }
    void ShaderResourceBinding::BindTexture(const std::string &name, Texture &texture) noexcept {
        this->BindTexture(name, texture, TextureSubresourceRange::GetFullRange());
    }

    void ShaderResourceBinding::BindTexture(
        const std::string &name, Texture &texture, TextureSubresourceRange range
    ) noexcept {
        pimpl->interfaces[name] = std::make_tuple(texture.GetImageView(range), texture.GetSampler());
    }

    vk::DescriptorSet ShaderResourceBinding::GetDescriptorSet(
        uint32_t set_id,
        vk::DescriptorSetLayout layout,
        const Rhi::SPLayout &s,
        bool enforce_dynamic_uniform,
        bool enforce_dynamic_storage
    ) {
        // Map the bound names onto the reflected layout: the arena can only key
        // on content it produced, so resolving is this class's job.
        std::vector<Rhi::ResolvedBinding> content;
        content.reserve(s.interfaces.size());

        for (const auto &pinterface : s.interfaces) {
            if (pinterface->layout_set != set_id) continue;

            auto itr = pimpl->interfaces.find(pinterface->name);
            if (itr == pimpl->interfaces.end()) {
                continue;
            }

            // The descriptor type and the image layout follow from the
            // reflected interface together with the enforced flags; the layout
            // description is not consulted.
            const auto resolved =
                ResolveInterfaceBinding(*pinterface, enforce_dynamic_uniform, enforce_dynamic_storage);
            if (!resolved) {
                assert(!dynamic_cast<const Rhi::SPInterfaceBuffer *>(pinterface.get()) && "Unknown buffer type.");
                continue;
            }

            if (resolved->is_image) {
                auto pimg = std::get_if<std::tuple<vk::ImageView, vk::Sampler>>(&itr->second);
                assert(pimg);
                auto popaque = dynamic_cast<const Rhi::SPInterfaceOpaque *>(pinterface.get());
                assert(popaque && popaque->array_size == 0);
                content.emplace_back(
                    Rhi::ResolvedBinding{
                        .binding = pinterface->layout_binding,
                        .type = resolved->type,
                        .image_view = std::get<0>(*pimg),
                        .sampler = std::get<1>(*pimg),
                        .image_layout = resolved->image_layout
                    }
                );
            } else {
                auto pbuf = std::get_if<std::tuple<vk::Buffer, size_t, size_t>>(&itr->second);
                assert(pbuf);

                auto [buffer, offset, range] = *pbuf;
                content.emplace_back(
                    Rhi::ResolvedBinding{
                        .binding = pinterface->layout_binding,
                        .type = resolved->type,
                        .buffer = buffer,
                        .offset = offset,
                        .range = range
                    }
                );
            }
        }

        return pimpl->arena->Acquire(layout, set_id, enforce_dynamic_uniform, enforce_dynamic_storage, content);
    }

} // namespace Engine::Rhi
