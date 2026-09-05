#include "Rhi/Pipeline/ComputeResourceBinding.h"

#include "Rhi/Buffer/ComputeBuffer.h"
#include "Rhi/Buffer/IndexedBuffer.h"
#include "Rhi/Buffer/StructuredBuffer.h"
#include "Rhi/Buffer/StructuredBufferPlacer.h"
#include "Rhi/Device/DeviceContext.h"
#include "Rhi/Device/DeviceInterface.h"
#include "Rhi/Pipeline/ComputeStage.h"
#include "Rhi/Pipeline/ShaderParameterLayout.h"
#include "Rhi/Pipeline/ShaderResourceBinding.h"

#include <array>
#include <bitset>
#include <cassert>
#include <format>
#include <unordered_map>
#include <variant>
#include <vulkan/vulkan.hpp>

namespace Engine::Rhi {
    struct ComputeResourceBinding::impl {
        constexpr static uint32_t MAX_SLOT_COUNT = 8;

        DeviceContext *device_context;
        ComputeStage *stage;
        uint32_t slot_count;

        std::array<vk::DescriptorSet, MAX_SLOT_COUNT> descriptor_sets{};
        std::unique_ptr<ShaderResourceBinding> p_srb{};
        std::unique_ptr<StructuredBuffer> p_buffer{};
        std::vector<std::byte> cpu_side_buffer{};

        // Manages UBO related stuff.
        struct {
            std::bitset<MAX_SLOT_COUNT> ubo_dirty{};
            std::unordered_map<std::string, std::unique_ptr<IndexedBuffer>> ubos{};

            void SetDirtyFlag() noexcept {
                ubo_dirty.set();
            }

            void PrepareIndexedBuffers(
                const DeviceInterface &device_interface,
                const AllocatorState &allocator_state,
                const Rhi::SPLayout &layout,
                uint32_t slot_count
            ) {
                ubos.clear();

                for (const auto &pinterface : layout.interfaces) {
                    if (auto pbuffer = dynamic_cast<const Rhi::SPInterfaceBuffer *>(pinterface.get())) {
                        if (pbuffer->type == Rhi::SPInterfaceBuffer::Type::UniformBuffer) {
                            auto psb = dynamic_cast<const Rhi::SPInterfaceStructuredBuffer *>(pinterface.get());
                            if (!psb) {
                                continue;
                            }

                            auto placer = psb->buffer_placer;
                            assert(placer);

                            ubos[pbuffer->name] = IndexedBuffer::CreateUnique(
                                allocator_state,
                                {BufferTypeBits::HostAccessibleUniform},
                                placer->GetBlockSize(),
                                device_interface.QueryLimit(
                                    Rhi::DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment
                                ),
                                slot_count,
                                std::format("Indexed UBO {} for Compute Shader", pbuffer->name)
                            );
                        }
                    }
                }
            }
        } ubo_manager{};

        // Holds ownership of resources.
        std::unordered_map<
            std::string,
            std::variant<std::shared_ptr<const Texture>, std::shared_ptr<const DeviceBuffer>>>
            owned_resource;
    };
    ComputeResourceBinding::ComputeResourceBinding(
        DeviceContext &device_context, ComputeStage &compute, uint32_t slot_count
    ) : pimpl(std::make_unique<impl>()) {
        assert(slot_count > 0 && slot_count <= impl::MAX_SLOT_COUNT);
        pimpl->p_srb = std::make_unique<ShaderResourceBinding>(device_context.GetIRCache());
        pimpl->p_buffer = std::make_unique<StructuredBuffer>();
        pimpl->device_context = &device_context;
        pimpl->stage = &compute;
        pimpl->slot_count = slot_count;
        pimpl->ubo_manager.PrepareIndexedBuffers(
            device_context.GetDeviceInterface(),
            device_context.GetAllocatorState(),
            compute.GetReflectedShaderInfo(),
            slot_count
        );
    }

    ComputeResourceBinding::~ComputeResourceBinding() noexcept = default;

    StructuredBuffer &ComputeResourceBinding::GetStructuredBuffer() noexcept {
        // We return a non-const lvalue reference, therefore writes are possible.
        pimpl->ubo_manager.SetDirtyFlag();
        return *pimpl->p_buffer;
    }

    const StructuredBuffer &ComputeResourceBinding::GetStructuredBuffer() const noexcept {
        return *pimpl->p_buffer;
    }

    ShaderResourceBinding &ComputeResourceBinding::GetShaderResourceBinding() noexcept {
        return *pimpl->p_srb;
    }
    void ComputeResourceBinding::BindTexture(const std::string &name, std::shared_ptr<Texture> texture) noexcept {
        pimpl->owned_resource[name] = texture;
        pimpl->p_srb->BindTexture(name, *texture);
    }
    void ComputeResourceBinding::BindComputeBuffer(
        const std::string &name, std::shared_ptr<const ComputeBuffer> buffer, size_t offset, size_t size
    ) {
        pimpl->owned_resource[name] = buffer;
        pimpl->p_srb->BindBuffer(name, *buffer, offset, size);
    }
    std::vector<uint32_t> ComputeResourceBinding::UpdateGPUInfo(uint32_t slot) const noexcept {
        assert(slot < pimpl->slot_count);
        // First prepare descriptor writes
        std::vector<uint32_t> dynamic_offsets;
        for (const auto &[k, v] : pimpl->ubo_manager.ubos) {
            pimpl->p_srb->BindBuffer(k, *v, 0, v->GetSliceSize());
            // FIXME: Dynamic offset order might not be correct.
            dynamic_offsets.push_back(v->GetSliceOffset(slot));
        }
        pimpl->descriptor_sets[slot] = pimpl->p_srb->GetDescriptorSet(
            0,
            pimpl->stage->GetReflectedShaderInfo(),
            pimpl->device_context->GetDevice(),
            pimpl->stage->GetDescriptorPool(),
            true,
            false
        );

        // Then do uniform writes.
        if (pimpl->ubo_manager.ubo_dirty[slot]) {
            const auto &splayout = pimpl->stage->GetReflectedShaderInfo();
            for (const auto &[k, v] : pimpl->ubo_manager.ubos) {
                auto itr = splayout.interface_name_mapping.find(k);
                assert(itr != splayout.interface_name_mapping.end());
                auto pbuf = dynamic_cast<const Rhi::SPInterfaceStructuredBuffer *>(itr->second);
                assert(pbuf && pbuf->type == Rhi::SPInterfaceBuffer::Type::UniformBuffer);
                splayout.PlaceBufferVariable(pimpl->cpu_side_buffer, *pbuf, *pimpl->p_buffer);
                std::memcpy(
                    v->GetSlicePtr(slot), this->pimpl->cpu_side_buffer.data(), this->pimpl->cpu_side_buffer.size()
                );
            }

            pimpl->ubo_manager.ubo_dirty[slot] = false;
        }
        return dynamic_offsets;
    }
    vk::DescriptorSet ComputeResourceBinding::GetDescriptorSet(uint32_t slot) const noexcept {
        assert(slot < pimpl->slot_count);
        return pimpl->descriptor_sets[slot];
    }
} // namespace Engine::Rhi
