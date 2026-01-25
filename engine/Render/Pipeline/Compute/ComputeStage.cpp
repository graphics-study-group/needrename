#include "ComputeStage.h"

#include "Asset/AssetRef.h"
#include "Asset/Shader/ShaderAsset.h"
#include "Render/Memory/DeviceBuffer.h"
#include "Render/DebugUtils.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/DeviceInterface.h"
#include "Render/Memory/IndexedBuffer.h"
#include "Render/Memory/StructuredBufferPlacer.h"
#include "Render/Memory/ShaderParameters/ShaderParameter.h"
#include "Render/Memory/ShaderParameters/ShaderParameterLayout.h"
#include <string>
#include <bitset>
#include <unordered_map>
#include <unordered_set>

#include <SDL3/SDL.h>

namespace Engine {

    struct ComputeStage::impl {
        struct InstancedPassInfo {
            static constexpr uint32_t BACK_BUFFERS = RenderSystemState::FrameManager::FRAMES_IN_FLIGHT;

            std::unordered_map <std::string, std::unique_ptr<IndexedBuffer>> ubos{};

            std::bitset<8> _is_ubo_dirty{};
            std::bitset<8> _is_descriptor_dirty{};

            std::array<vk::DescriptorSet, BACK_BUFFERS> desc_sets {};
        } m_ipi {};
        
        std::vector <std::byte> m_ubo_staging_buffer{};

        PassInfo m_passInfo{};
        // This will create a lot of allocations of descriptor pool.
        // We might need to optimize it a little.
        vk::UniqueDescriptorPool desc_pool {};

        ShdrRfl::SPLayout layout{};
        ShdrRfl::ShaderParameters parameters{};

        void CreatePipeline(RenderSystem &system, const std::vector <uint32_t> & spirv_code, const std::string_view name = "") {
            // Create descriptor and pipeline layout
            layout = ShdrRfl::SPLayout::Reflect(spirv_code, false);
            auto desc_bindings = layout.GenerateAllLayoutBindings();
            if (desc_bindings.size() > 1) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER,
                    "Found multiple descriptor sets. Only zeroth set is currently utilized."
                );
            }
            assert(desc_bindings.contains(0) && "Descriptor set zero is empty.");
            vk::DescriptorSetLayoutCreateInfo dslci{
                vk::DescriptorSetLayoutCreateFlags{},
                desc_bindings[0]
            };
            m_passInfo.desc_layout = system.GetDevice().createDescriptorSetLayoutUnique(dslci);

            vk::PipelineLayoutCreateInfo plci{vk::PipelineLayoutCreateFlags{}, {m_passInfo.desc_layout.get()}, {}};
            m_passInfo.pipeline_layout = system.GetDevice().createPipelineLayoutUnique(plci);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                m_passInfo.pipeline_layout.get(),
                std::format("Pipeline Layout for Compute {}", name)
            );

            // Create descriptor pool
            std::unordered_map <vk::DescriptorType, uint32_t> descriptor_pool_size;
            std::vector <vk::DescriptorPoolSize> flattened_descriptor_pool_size;
            for (auto b : desc_bindings[0]) {
                descriptor_pool_size[b.descriptorType]++;
            }
            flattened_descriptor_pool_size.reserve(descriptor_pool_size.size());
            for (const auto & b : descriptor_pool_size) {
                flattened_descriptor_pool_size.push_back(vk::DescriptorPoolSize{b.first, b.second * InstancedPassInfo::BACK_BUFFERS});
            }
            vk::DescriptorPoolCreateInfo dpci {
                vk::DescriptorPoolCreateFlags{},
                InstancedPassInfo::BACK_BUFFERS,
                flattened_descriptor_pool_size
            };
            desc_pool = system.GetDevice().createDescriptorPoolUnique(dpci);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                desc_pool.get(),
                std::format("Descriptor Pool for Compute Pipeline {}", name)
            );

            // Create shader module
            vk::ShaderModuleCreateInfo smci{
                vk::ShaderModuleCreateFlags{},
                spirv_code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *>(spirv_code.data())
            };
            m_passInfo.shader = system.GetDevice().createShaderModuleUnique(smci);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                m_passInfo.shader.get(),
                std::format("Shader Module for Compute Pipeline {}", name)
            );

            vk::PipelineShaderStageCreateInfo pssci{
                vk::PipelineShaderStageCreateFlags{},
                vk::ShaderStageFlagBits::eCompute,
                m_passInfo.shader.get(),
                "main"
            };
            vk::ComputePipelineCreateInfo cpci{vk::PipelineCreateFlags{}, pssci, m_passInfo.pipeline_layout.get()};
            auto ret = system.GetDevice().createComputePipelineUnique(nullptr, cpci);
            m_passInfo.pipeline = std::move(ret.value);
            DEBUG_SET_NAME_TEMPLATE(
                system.GetDevice(),
                m_passInfo.pipeline.get(),
                std::format("Compute Pipeline {}", name)
            );
        }
    };

    ComputeStage::ComputeStage(RenderSystem &system) : m_system(system), pimpl(std::make_unique<ComputeStage::impl>()) {
    }

    void ComputeStage::Instantiate(const ShaderAsset &asset) {
        assert(asset.shaderType == ShaderAsset::ShaderType::Compute);
        Instantiate(asset.binary, asset.m_name);
    }

    void ComputeStage::Instantiate(const std::vector<uint32_t> &code, const std::string_view name) {
        pimpl->CreatePipeline(m_system, code, name);

        for (const auto & pinterface : pimpl->layout.interfaces) {
            if (auto pbuffer = dynamic_cast <const ShdrRfl::SPInterfaceBuffer *>(pinterface.get())) {
                if (pbuffer->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer) {
                    auto psb = dynamic_cast<const ShdrRfl::SPInterfaceStructuredBuffer *>(pinterface.get());
                    if (!psb) {
                        SDL_LogWarn(
                            SDL_LOG_CATEGORY_RENDER,
                            "Uniform buffer named %s is not structured, and cannot be manipulated.",
                            pbuffer->name.c_str()
                        );
                        continue;
                    }

                    auto placer = psb->buffer_placer;
                    assert(placer);

                    pimpl->m_ipi.ubos[pbuffer->name] = IndexedBuffer::CreateUnique(
                        m_system.GetAllocatorState(),
                        {BufferTypeBits::HostAccessibleUniform},
                        placer->CalculateMaxSize(),
                        m_system.GetDeviceInterface().QueryLimit(
                            RenderSystemState::DeviceInterface::PhysicalDeviceLimitInteger::UniformBufferOffsetAlignment
                        ),
                        impl::InstancedPassInfo::BACK_BUFFERS,
                        std::format(
                            "Indexed UBO {} for Material",
                            pbuffer->name
                        )
                    );
                }
            }
        }

        std::array <vk::DescriptorSetLayout, impl::InstancedPassInfo::BACK_BUFFERS> layouts{};
        std::fill(layouts.begin(), layouts.end(), pimpl->m_passInfo.desc_layout.get());
        vk::DescriptorSetAllocateInfo dsai{
            pimpl->desc_pool.get(), layouts
        };
        auto desc_sets = m_system.GetDevice().allocateDescriptorSets(dsai);

        assert(desc_sets.size() == impl::InstancedPassInfo::BACK_BUFFERS);
        std::copy_n(desc_sets.begin(), pimpl->m_ipi.desc_sets.size(), pimpl->m_ipi.desc_sets.begin());

        pimpl->m_ipi._is_descriptor_dirty.set();
        pimpl->m_ipi._is_ubo_dirty.set();
    }

    ComputeStage::~ComputeStage() = default;

    void ComputeStage::UpdateGPUInfo(uint32_t backbuffer) {
        assert(backbuffer < impl::InstancedPassInfo::BACK_BUFFERS);
        // Maybe we should reorganize and reuse these loc.
        // First prepare descriptor writes
        if (pimpl->m_ipi._is_descriptor_dirty[backbuffer]) {
            // Point UBOs to internal buffers
            for (const auto & kv : pimpl->m_ipi.ubos) {
                this->pimpl->parameters.Assign(
                    kv.first, 
                    *(kv.second.get()),
                    kv.second->GetSliceOffset(backbuffer),
                    kv.second->GetSliceSize()
                );
            }
            auto writes_from_layout = pimpl->layout.GenerateDescriptorSetWrite(0, pimpl->parameters);

            std::vector <vk::WriteDescriptorSet> vk_writes {writes_from_layout.buffer.size() + writes_from_layout.image.size()};
            std::vector <std::array<vk::DescriptorBufferInfo, 1>> vk_buffer_writes {writes_from_layout.buffer.size()};

            size_t write_count = 0;
            for (const auto & w : writes_from_layout.buffer) {
                vk_buffer_writes[write_count][0] = vk::DescriptorBufferInfo{
                        std::get<1>(w).buffer,
                        std::get<1>(w).offset,
                        std::get<1>(w).range
                    };
                vk_writes[write_count] = vk::WriteDescriptorSet{
                    pimpl->m_ipi.desc_sets[backbuffer],
                    std::get<0>(w),
                    0,
                    std::get<2>(w),
                    {},
                    vk_buffer_writes[write_count],
                    {}
                };
                write_count ++;
            }

            for (const auto & w : writes_from_layout.image) {
                vk_writes[write_count] = vk::WriteDescriptorSet {
                    pimpl->m_ipi.desc_sets[backbuffer],
                    std::get<0>(w),
                    0,
                    std::get<2>(w),
                    { std::get<1>(w) }
                };
                write_count ++;
            }
            m_system.GetDevice().updateDescriptorSets(vk_writes, {});
            pimpl->m_ipi._is_descriptor_dirty[backbuffer] = false;
        }

        // Then do UBO buffer writes
        if (pimpl->m_ipi._is_ubo_dirty[backbuffer]) {

            for (const auto & kv : this->pimpl->m_ipi.ubos) {
                auto itr = pimpl->layout.interface_name_mapping.find(kv.first);
                assert(itr != pimpl->layout.interface_name_mapping.end());
                auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceStructuredBuffer *>(itr->second);
                assert(pbuf && pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer);

                pimpl->layout.PlaceBufferVariable(
                    this->pimpl->m_ubo_staging_buffer,
                    *pbuf,
                    this->pimpl->parameters
                );

                std::memcpy(
                    kv.second->GetSlicePtr(backbuffer),
                    this->pimpl->m_ubo_staging_buffer.data(),
                    this->pimpl->m_ubo_staging_buffer.size()
                );
            }
            
            pimpl->m_ipi._is_ubo_dirty[backbuffer] = false;
        }
    }

    void ComputeStage::AssignScalarVariable(
        const std::string & name,
        std::variant<uint32_t, float> value) noexcept {
        
        struct Visitor {
            impl* pimpl;
            const std::string & name;

            void operator () (uint32_t v) {
                pimpl->parameters.Assign(name, v);
            };
            void operator () (float v) {
                pimpl->parameters.Assign(name, v);
            };
        };

        std::visit(Visitor{pimpl.get(), name}, value);
        pimpl->m_ipi._is_ubo_dirty.set();
    }
    void ComputeStage::AssignVectorVariable(
        const std::string & name,
        std::variant<glm::vec4, glm::mat4> value) noexcept {

        struct Visitor {
            impl* pimpl;
            const std::string & name;

            void operator () (const glm::vec4 & v) {
                pimpl->parameters.Assign(name, v);
            };
            void operator () (const glm::mat4 & v) {
                pimpl->parameters.Assign(name, v);
            };
        };

        std::visit(Visitor{pimpl.get(), name}, value);
        pimpl->m_ipi._is_ubo_dirty.set();
    }
    void ComputeStage::AssignTexture(
        const std::string & name, 
        std::shared_ptr <const Texture> texture) noexcept {
        pimpl->parameters.Assign(name, texture);
        pimpl->m_ipi._is_descriptor_dirty.set();
    }
    void ComputeStage::AssignBuffer(
        const std::string & name,
        std::shared_ptr <const DeviceBuffer> buffer) noexcept {
        pimpl->parameters.Assign(name, buffer);
        pimpl->m_ipi._is_descriptor_dirty.set();
    }

    void ComputeStage::AssignComputeBuffer(const std::string &name, const ComputeBuffer &buffer) noexcept {
        pimpl->parameters.Assign(name, buffer);
        pimpl->m_ipi._is_descriptor_dirty.set();
    }

    vk::Pipeline ComputeStage::GetPipeline() const noexcept {
        return pimpl->m_passInfo.pipeline.get();
    }
    vk::PipelineLayout ComputeStage::GetPipelineLayout() const noexcept {
        return pimpl->m_passInfo.pipeline_layout.get();
    }
    vk::DescriptorSetLayout ComputeStage::GetDescriptorSetLayout() const noexcept {
        return pimpl->m_passInfo.desc_layout.get();
    }
    vk::DescriptorSet ComputeStage::GetDescriptorSet(uint32_t backbuffer) const noexcept {
        assert(backbuffer < impl::InstancedPassInfo::BACK_BUFFERS);
        return pimpl->m_ipi.desc_sets[backbuffer];
    }
} // namespace Engine
