#include "ShaderResourceBinding.h"

#include "Render/Hasher.hpp"
#include "Render/Memory/ShaderParameters/ShaderInterface.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"

#include <vulkan/vulkan.hpp>

namespace Engine {
    struct ShaderResourceBinding::impl {
        RenderSystemState::ImmutableResourceCache * irc;

        using InterfaceVariant = std::variant<
            std::monostate,
            std::reference_wrapper<const Texture>,
            // Buffer, offset and size
            std::tuple<std::reference_wrapper<const DeviceBuffer>, size_t, size_t>
        >;

        // This map has to be ordered to ensure consistent hash.
        std::map <std::string, InterfaceVariant> interfaces;

        void hash_current_interfaces(RenderResourceHasher & h) const noexcept {
            struct HashVisitor {
                RenderResourceHasher * h;

                void operator () (std::monostate) {
                }
                void operator () (const std::reference_wrapper<const Texture> & r) {
                    h->pointer(&r.get());
                }
                void operator () (const std::tuple<std::reference_wrapper<const DeviceBuffer>, size_t, size_t> & r) {
                    h->pointer(&std::get<0>(r).get());
                    h->u64(std::get<1>(r));
                    h->u64(std::get<2>(r));
                }
            };

            for (const auto & [k, v] : interfaces) {
                h.string(k);
                std::visit(HashVisitor{&h}, v);
            }
        }
        
        std::unordered_map <size_t,
            // XXX: use a LRU cache instead of unordered map here, to mitigate memory leak.
            std::unordered_map<size_t, vk::DescriptorSet>
        > descriptor_sets;
    };

    ShaderResourceBinding::ShaderResourceBinding(
        RenderSystemState::ImmutableResourceCache & irc
    ) : pimpl(std::make_unique<impl>()){
        pimpl->irc = &irc;
    }

    ShaderResourceBinding::~ShaderResourceBinding() noexcept = default;

    void ShaderResourceBinding::BindBuffer(
        const std::string &name,
        const DeviceBuffer &buf,
        size_t offset,
        size_t size
    ) noexcept {
        static_assert(vk::WholeSize == std::numeric_limits<size_t>::max());
        pimpl->interfaces[name] = std::make_tuple(std::cref(buf), offset, size);
    }
    void ShaderResourceBinding::BindTexture(
        const std::string &name,
        const Texture &texture
    ) noexcept {
        pimpl->interfaces[name] = std::cref(texture);
    }

    vk::DescriptorSet ShaderResourceBinding::GetDescriptorSet(
        uint32_t set_id,
        const ShdrRfl::SPLayout & s,
        vk::Device d,
        vk::DescriptorPool pool,
        bool enforce_dynamic_uniform,
        bool enforce_dynamic_storage
    ) {
        // First calculate a hash from currently bound resources.
        RenderResourceHasher h;
        h.pointer(&s);
        // Set id won't be too large, so this might be safe.
        h.u32((enforce_dynamic_uniform << 31u) | (enforce_dynamic_storage << 30u) | set_id);
        auto layout_hash = h.get();

        RenderResourceHasher ch;
        pimpl->hash_current_interfaces(ch);
        auto content_hash = ch.get();

        // Return a cache hit
        if (pimpl->descriptor_sets[layout_hash].contains(content_hash)) {
            return pimpl->descriptor_sets[layout_hash][content_hash];
        }

        // Get the descriptor set layout for the descriptor set
        auto dslb = s.GenerateLayoutBindings(
            set_id,
            enforce_dynamic_uniform,
            enforce_dynamic_storage
        );

        // Produce descriptor writes
        std::vector <vk::DescriptorImageInfo> image_infos;
        std::vector <vk::DescriptorBufferInfo> buffer_infos;
        std::vector <vk::WriteDescriptorSet> writes;
        for (const auto & pinterface : s.interfaces) {
            if (pinterface->layout_set != set_id) continue;

            auto & intfc = pimpl->interfaces;
            auto itr = intfc.find(pinterface->name);
            if (itr == intfc.end()) {
                continue;
            }

            if (auto popaque = dynamic_cast<const ShdrRfl::SPInterfaceOpaqueImage *>(pinterface.get())) {
                auto pimg = std::get_if<std::reference_wrapper<const Texture>>(&itr->second);
                assert(pimg);
                assert(popaque->array_size == 0);
                image_infos.push_back(
                    vk::DescriptorImageInfo{
                        pimg->get().GetSampler(),
                        pimg->get().GetImageView(),
                        vk::ImageLayout::eReadOnlyOptimal
                    }
                );
                writes.push_back(
                    vk::WriteDescriptorSet{
                        nullptr,
                        popaque->layout_binding,
                        0u,     // 0th element
                        1u,     // 1 descriptor
                        vk::DescriptorType::eCombinedImageSampler
                    }
                );
                writes.back().setPImageInfo(&image_infos.back());
            } else if (auto pstorage = dynamic_cast<const ShdrRfl::SPInterfaceOpaqueStorageImage *>(pinterface.get())) {
                auto pimg = std::get_if<std::reference_wrapper<const Texture>>(&itr->second);
                assert(pimg);
                assert(pstorage->array_size == 0);
                assert((pimg)->get().SupportRandomAccess());

                image_infos.push_back(
                    vk::DescriptorImageInfo{
                        pimg->get().GetSampler(),
                        pimg->get().GetImageView(),
                        vk::ImageLayout::eGeneral
                    }
                );
                writes.push_back(
                    vk::WriteDescriptorSet{
                        nullptr,
                        popaque->layout_binding,
                        0u,     // 0th element
                        1u,     // 1 descriptor
                        vk::DescriptorType::eStorageImage
                    }
                );
                writes.back().setPImageInfo(&image_infos.back());
            } 
            // The interface is a buffer
            else if (auto pbuffer = dynamic_cast<const ShdrRfl::SPInterfaceBuffer *>(pinterface.get())) {
                auto pbuf = std::get_if<std::tuple<std::reference_wrapper<const DeviceBuffer>, size_t, size_t>>(&itr->second);
                assert(pbuf);

                // Determine whether it is storage buffer or uniform buffer and static or dynamic
                auto itr = std::find_if(
                    dslb.begin(), dslb.end(),
                    [&pbuffer](const vk::DescriptorSetLayoutBinding & p) -> bool {
                        return p.binding == pbuffer->layout_binding;
                    }
                );
                assert(itr != dslb.end());
                auto desctp = itr->descriptorType;
                auto [buffer, offset, range] = *pbuf;

                // TODO: Test for buffer type (storage vs uniform)

                buffer_infos.push_back(
                    vk::DescriptorBufferInfo {
                        std::get<0>(*pbuf).get().GetBuffer(),
                        offset,
                        range
                    }
                );
                writes.push_back(
                    vk::WriteDescriptorSet{
                        nullptr,
                        pbuffer->layout_binding,
                        0u,     // 0th element
                        1u,     // 1 descriptor
                        desctp
                    }
                );
                writes.back().setPBufferInfo(&buffer_infos.back());
            }
        }

        auto dsl = pimpl->irc->GetDescriptorSetLayout(
            vk::DescriptorSetLayoutCreateInfo{
                vk::DescriptorSetLayoutCreateFlags{},
                dslb
            }
        );
        // Allocate descriptor set
        assert(pool);
        vk::DescriptorSetAllocateInfo dsai{pool, {dsl}};
        auto descriptor = d.allocateDescriptorSets(dsai)[0];
        pimpl->descriptor_sets[layout_hash][content_hash] = descriptor;

        for (auto & w : writes) {
            w.dstSet = descriptor;
        }

        d.updateDescriptorSets(writes, {});
        return descriptor;
    }

} // namespace Engine
