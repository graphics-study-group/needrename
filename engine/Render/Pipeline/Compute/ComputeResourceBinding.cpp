#include "ComputeResourceBinding.h"

#include "Render/Memory/IndexedBuffer.h"
#include "Render/Memory/StructuredBuffer.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include "Render/Memory/ShaderParameters/ShaderResourceBinding.h"

#include <bitset>
#include <unordered_map>

namespace Engine {
    struct ComputeResourceBinding::impl {
        constexpr static uint32_t BACK_BUFFERS = 3;

        RenderSystem * system;
        ComputeStage * stage;
        
        std::array <vk::DescriptorSet, BACK_BUFFERS> descriptor_sets{};
        std::unique_ptr <ShaderResourceBinding> p_srb{};
        std::unique_ptr <StructuredBuffer> p_buffer{};
        std::vector <std::byte> cpu_side_buffer{};

        // Manages UBO related stuff.
        struct {
            std::bitset<8> ubo_dirty{};
            std::unordered_map <
                std::string, std::unique_ptr<IndexedBuffer>
            > ubos {};

            void SetDirtyFlag() noexcept {
                ubo_dirty.set();
            }

            void PrepareIndexedBuffers(
                RenderSystem & system,
                const ShdrRfl::SPLayout & layout
            ) {
                ubos.clear();

                for (const auto & pinterface : layout.interfaces) {
                    if (auto pbuffer = dynamic_cast <const ShdrRfl::SPInterfaceBuffer *>(pinterface.get())) {
                        if (pbuffer->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer) {
                            auto psb = dynamic_cast<const ShdrRfl::SPInterfaceStructuredBuffer *>(pinterface.get());
                            if (!psb) {
                                continue;
                            }

                            auto placer = psb->buffer_placer;
                            assert(placer);

                            ubos[pbuffer->name] = IndexedBuffer::CreateUnique(
                                system.GetAllocatorState(),
                                {BufferTypeBits::HostAccessibleUniform},
                                placer->CalculateMaxSize(),
                                system.GetDeviceInterface().QueryLimit(
                                    RenderSystemState::DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment
                                ),
                                BACK_BUFFERS,
                                std::format(
                                    "Indexed UBO {} for Compute Shader",
                                    pbuffer->name
                                )
                            );
                        }
                    }
                }
            }
        } ubo_manager {};

        // Holds ownership of resources.
        std::unordered_map <std::string,
            std::variant<
                std::shared_ptr<const Texture>,
                std::shared_ptr<const DeviceBuffer>
            >
        > owned_resource;
    };
    ComputeResourceBinding::ComputeResourceBinding(
        RenderSystem &system,
        ComputeStage &compute
    ) : pimpl(std::make_unique<impl>()) {
        pimpl->p_srb = std::make_unique<ShaderResourceBinding>(system.GetIRCache());
        pimpl->p_buffer = std::make_unique<StructuredBuffer>();
        pimpl->system = &system;
        pimpl->stage = &compute;
        pimpl->ubo_manager.PrepareIndexedBuffers(system, compute.GetReflectedShaderInfo());
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

    ShaderResourceBinding & ComputeResourceBinding::GetShaderResourceBinding() noexcept {
        return *pimpl->p_srb;
    }
    void ComputeResourceBinding::BindTexture(
        const std::string &name,
        std::shared_ptr<const Texture> texture
    ) noexcept {
        pimpl->owned_resource[name] = texture;
        pimpl->p_srb->BindTexture(name, *texture);
    }
    void ComputeResourceBinding::BindComputeBuffer(
        const std::string &name, std::shared_ptr<const ComputeBuffer> buffer, size_t offset, size_t size
    ) {
        pimpl->owned_resource[name] = buffer;
        pimpl->p_srb->BindBuffer(name, *buffer, offset, size);
    }
    std::vector<uint32_t> ComputeResourceBinding::UpdateGPUInfo(uint32_t backbuffer) const noexcept {
        // First prepare descriptor writes
        std::vector <uint32_t> dynamic_offsets;
        for (const auto & [k, v] : pimpl->ubo_manager.ubos) {
            pimpl->p_srb->BindBuffer(
                k,
                *v,
                0,
                v->GetSliceSize()
            );
            // FIXME: Dynamic offset order might not be correct.
            dynamic_offsets.push_back(v->GetSliceOffset(backbuffer));
        }
        pimpl->descriptor_sets[backbuffer] = pimpl->p_srb->GetDescriptorSet(
            0,
            pimpl->stage->GetReflectedShaderInfo(),
            pimpl->system->GetDevice(),
            pimpl->stage->GetDescriptorPool(),
            true, false
        );

        // Then do uniform writes.
        if (pimpl->ubo_manager.ubo_dirty[backbuffer]) {
            const auto & splayout = pimpl->stage->GetReflectedShaderInfo();
            for (const auto & [k, v] : pimpl->ubo_manager.ubos) {
                auto itr = splayout.interface_name_mapping.find(k);
                assert(itr != splayout.interface_name_mapping.end());
                auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceStructuredBuffer *>(itr->second);
                assert(pbuf && pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer);
                splayout.PlaceBufferVariable(
                    pimpl->cpu_side_buffer,
                    *pbuf,
                    *pimpl->p_buffer
                );
                std::memcpy(
                    v->GetSlicePtr(backbuffer),
                    this->pimpl->cpu_side_buffer.data(),
                    this->pimpl->cpu_side_buffer.size()
                );
            }
            
            pimpl->ubo_manager.ubo_dirty[backbuffer] = false;
        }
        return dynamic_offsets;
    }
    vk::DescriptorSet ComputeResourceBinding::GetDescriptorSet(uint32_t b) const noexcept {
        return pimpl->descriptor_sets[b];
    }
} // namespace Engine
