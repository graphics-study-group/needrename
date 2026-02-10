#include "ShaderResourceBinding.h"

#include "Render/Hasher.hpp"
#include "Render/Memory/ShaderParameters/ShaderInterface.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"

#include <vulkan/vulkan.hpp>

namespace Engine {
    struct ShaderResourceBinding::impl {
        const ShdrRfl::SPLayout * layout;

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
        
        std::unordered_map <size_t, vk::DescriptorSet> descriptor_sets;
    };

    ShaderResourceBinding::ShaderResourceBinding(
        const ShdrRfl::SPLayout &s
    ) : pimpl(std::make_unique<impl>()){
        pimpl->layout = &s;
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
        vk::Device d,
        vk::DescriptorSetLayout dsl,
        vk::DescriptorPool pool
    ) {
        // First calculate a hash from currently bound resources.
        RenderResourceHasher h;
        h.handle(dsl);
        pimpl->hash_current_interfaces(h);
        auto hash = h.get();

        // Return a cache hit
        if (pimpl->descriptor_sets.contains(hash)) {
            return pimpl->descriptor_sets[hash];
        }

        // Allocate descriptor and write to it.
        vk::DescriptorSetAllocateInfo dsai{pool, {dsl}};
        auto descriptor = d.allocateDescriptorSets(dsai)[0];
        pimpl->descriptor_sets[hash] = descriptor;

        // Produce descriptor writes
        std::vector <vk::DescriptorImageInfo> image_infos;
        std::vector <vk::DescriptorBufferInfo> buffer_infos;
        std::vector <vk::WriteDescriptorSet> writes;
        for (const auto & pinterface : pimpl->layout->interfaces) {
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
                        descriptor,
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
                        descriptor,
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

                // Determine whether it is storage buffer or uniform buffer
                auto desctp = 
                    pbuffer->type == ShdrRfl::SPInterfaceBuffer::Type::StorageBuffer ?
                    vk::DescriptorType::eStorageBuffer : vk::DescriptorType::eUniformBuffer;
                auto [buffer, offset, range] = *pbuf;

                buffer_infos.push_back(
                    vk::DescriptorBufferInfo {
                        std::get<0>(*pbuf).get().GetBuffer(),
                        offset,
                        range
                    }
                );
                writes.push_back(
                    vk::WriteDescriptorSet{
                        descriptor,
                        popaque->layout_binding,
                        0u,     // 0th element
                        1u,     // 1 descriptor
                        desctp
                    }
                );
                writes.back().setPBufferInfo(&buffer_infos.back());
            }
        }

        d.updateDescriptorSets(writes, {});
        return descriptor;
    }

} // namespace Engine
