#include "ComputeStage.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/ShaderAsset.h"
#include "Render/Memory/Buffer.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
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
            static constexpr uint32_t BACK_BUFFERS = 3;

            std::unordered_map <std::string, std::unique_ptr<IndexedBuffer>> ubos{};

            std::bitset<8> _is_ubo_dirty{};
            std::bitset<8> _is_descriptor_dirty{};

            std::array<vk::DescriptorSet, BACK_BUFFERS> desc_sets{};
        } m_ipi;
        
        std::vector <std::byte> m_ubo_staging_buffer{};

        PassInfo m_passInfo{};
        ShdrRfl::SPLayout layout{};
        ShdrRfl::ShaderParameters parameters{};

        void CreatePipeline(RenderSystem &system, const ShaderAsset &asset) {
            assert(asset.shaderType == ShaderAsset::ShaderType::Compute);
            auto code = asset.binary;

            layout = ShdrRfl::SPLayout::Reflect(code, false);
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
            m_passInfo.desc_layout = system.getDevice().createDescriptorSetLayoutUnique(dslci);

            vk::PipelineLayoutCreateInfo plci{vk::PipelineLayoutCreateFlags{}, {m_passInfo.desc_layout.get()}, {}};
            m_passInfo.pipeline_layout = system.getDevice().createPipelineLayoutUnique(plci);

            vk::ShaderModuleCreateInfo smci{
                vk::ShaderModuleCreateFlags{},
                code.size() * sizeof(uint32_t),
                reinterpret_cast<const uint32_t *>(code.data())
            };
            m_passInfo.shaders.resize(1);
            m_passInfo.shaders[0] = system.getDevice().createShaderModuleUnique(smci);

            vk::PipelineShaderStageCreateInfo pssci{
                vk::PipelineShaderStageCreateFlags{},
                vk::ShaderStageFlagBits::eCompute,
                m_passInfo.shaders[0].get(),
                "main"
            };
            vk::ComputePipelineCreateInfo cpci{vk::PipelineCreateFlags{}, pssci, m_passInfo.pipeline_layout.get()};
            auto ret = system.getDevice().createComputePipelineUnique(nullptr, cpci);
            m_passInfo.pipeline = std::move(ret.value);
        }
    };

    ComputeStage::ComputeStage(RenderSystem &system) : m_system(system), pimpl(std::make_unique<ComputeStage::impl>()) {
    }

    void ComputeStage::Instantiate(const ShaderAsset &asset) {
        pimpl->CreatePipeline(m_system, asset);

        for (auto pinterface : pimpl->layout.interfaces) {
            if (auto pbuffer = dynamic_cast <const ShdrRfl::SPInterfaceBuffer *>(pinterface)) {
                if (pbuffer->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer) {
                    auto ptype = dynamic_cast <const ShdrRfl::SPTypeSimpleStruct *>(pbuffer->underlying_type);
                    assert(ptype);

                    pimpl->m_ipi.ubos[pbuffer->name] = IndexedBuffer::CreateUnique(
                        m_system,
                        Buffer::BufferType::Uniform,
                        ptype->expected_size,
                        m_system.GetPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment,
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
            m_system.GetGlobalConstantDescriptorPool().get(), layouts
        };
        auto desc_sets = m_system.getDevice().allocateDescriptorSets(dsai);

        assert(desc_sets.size() == impl::InstancedPassInfo::BACK_BUFFERS);
        for (int i = 0; i < impl::InstancedPassInfo::BACK_BUFFERS; i++) {
            pimpl->m_ipi.desc_sets[i] = desc_sets[i];
        }

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
            m_system.getDevice().updateDescriptorSets(vk_writes, {});
            pimpl->m_ipi._is_descriptor_dirty[backbuffer] = false;
        }

        // Then do UBO buffer writes
        if (pimpl->m_ipi._is_ubo_dirty[backbuffer]) {

            for (const auto & kv : this->pimpl->m_ipi.ubos) {
                auto itr = pimpl->layout.name_mapping.find(kv.first);
                assert(itr != pimpl->layout.name_mapping.end());
                auto pbuf = dynamic_cast<const ShdrRfl::SPInterfaceBuffer *>(itr->second);
                assert(pbuf && pbuf->type == ShdrRfl::SPInterfaceBuffer::Type::UniformBuffer);

                pimpl->layout.PlaceBufferVariable(
                    this->pimpl->m_ubo_staging_buffer,
                    pbuf,
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
        std::variant<uint32_t, int32_t, float> value) noexcept {
        pimpl->parameters.Assign(name, value);
        pimpl->m_ipi._is_ubo_dirty.set();
    }
    void ComputeStage::AssignVectorVariable(
        const std::string & name,
        std::variant<glm::vec4, glm::mat4> value) noexcept {
        pimpl->parameters.Assign(name, value);
        pimpl->m_ipi._is_ubo_dirty.set();
    }
    void ComputeStage::AssignTexture(
        const std::string & name, 
        const Texture & texture) noexcept {
        pimpl->parameters.Assign(name, texture);
        pimpl->m_ipi._is_descriptor_dirty.set();
    }
    void ComputeStage::AssignBuffer(
        const std::string & name,
        const Buffer & buffer) noexcept {
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
