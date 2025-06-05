#include "ComputeStage.h"

#include "Asset/AssetRef.h"
#include "Asset/Material/ShaderAsset.h"
#include "Render/RenderSystem.h"
#include "Render/Memory/Buffer.h"
#include "Render/RenderSystem/GlobalConstantDescriptorPool.h"
#include <unordered_set>

#include <SDL3/SDL.h>

namespace Engine{

    struct ComputeStage::impl {
        std::unordered_map <uint32_t, std::any> m_desc_variables {};
        std::unordered_map <uint32_t, std::any> m_inblock_variables {};

        std::vector <std::byte> m_ubo_staging_buffer;

        PassInfo m_passInfo {};
        InstancedPassInfo m_instancedPassInfo {};
    };

    void ComputeStage::CreatePipeline()
    {
        auto shader_ref = m_asset->cas<ShaderAsset>();
        assert(shader_ref->shaderType == ShaderAsset::ShaderType::Compute);
        auto code = shader_ref->binary;

        auto reflected = ShaderUtils::ReflectSpirvDataCompute(code);
        // Save uniform locations (Reused from MaterialTemplate)
        #ifndef NDEBUG
        std::unordered_set <std::string> names;
        for (auto & [name, _] : reflected.inblock.names) {
            if (names.find(name) != names.end()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name %s", name.c_str());
            }
            names.insert(name);
        }
        for (auto & [name, _] : reflected.desc.names) {
            if (names.find(name) != names.end()) {
                SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Duplicated variable name %s", name.c_str());
            }
            names.insert(name);
        }
        #endif
        for (auto & [_, idx] : reflected.inblock.names) {
            idx += pimpl->m_passInfo.inblock.vars.size();
        }
        pimpl->m_passInfo.inblock.names.merge(reflected.inblock.names);
        pimpl->m_passInfo.inblock.vars.insert(pimpl->m_passInfo.inblock.vars.end(), reflected.inblock.vars.begin(), reflected.inblock.vars.end());

        for (auto & [_, idx] : reflected.desc.names) {
            idx += pimpl->m_passInfo.desc.vars.size();
        }
        pimpl->m_passInfo.desc.names.merge(reflected.desc.names);
        pimpl->m_passInfo.desc.vars.insert(pimpl->m_passInfo.desc.vars.end(), reflected.desc.vars.begin(), reflected.desc.vars.end());
        
        pimpl->m_passInfo.inblock.maximal_ubo_size = 0;
        for (const auto & var : pimpl->m_passInfo.inblock.vars) {
            pimpl->m_passInfo.inblock.maximal_ubo_size = std::max(
                pimpl->m_passInfo.inblock.maximal_ubo_size, 
                1ULL * var.inblock_location.offset + var.inblock_location.size
            );
        }

        vk::DescriptorSetLayoutCreateInfo dslci = reflected.set_layout.create_info;
        pimpl->m_passInfo.desc_layout = m_system.getDevice().createDescriptorSetLayoutUnique(dslci);

        vk::PipelineLayoutCreateInfo plci{
            vk::PipelineLayoutCreateFlags{},
            { pimpl->m_passInfo.desc_layout.get() },
            {}
        };
        pimpl->m_passInfo.pipeline_layout = m_system.getDevice().createPipelineLayoutUnique(plci);

        vk::ShaderModuleCreateInfo smci {
            vk::ShaderModuleCreateFlags{},
            code.size() * sizeof(uint32_t),
            reinterpret_cast<const uint32_t *> (code.data())
        };
        pimpl->m_passInfo.shaders.resize(1);
        pimpl->m_passInfo.shaders[0] = m_system.getDevice().createShaderModuleUnique(smci);

        vk::PipelineShaderStageCreateInfo pssci{
            vk::PipelineShaderStageCreateFlags{},
            vk::ShaderStageFlagBits::eCompute,
            pimpl->m_passInfo.shaders[0].get(),
            "main"
        };
        vk::ComputePipelineCreateInfo cpci{
            vk::PipelineCreateFlags{},
            pssci,
            pimpl->m_passInfo.pipeline_layout.get()
        };
        auto ret = m_system.getDevice().createComputePipelineUnique(nullptr, cpci);
        pimpl->m_passInfo.pipeline = std::move(ret.value);
    }

    ComputeStage::ComputeStage(
        RenderSystem &system, 
        std::shared_ptr<AssetRef> asset
    ) : m_system(system), m_asset(asset), pimpl(std::make_unique<ComputeStage::impl>())
    {
        CreatePipeline();

        // Allocate uniform buffer and descriptor set
        auto ubo_size = pimpl->m_passInfo.inblock.maximal_ubo_size;
        if (ubo_size == 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Found zero-sized UBO when processing compute stage shader %s.", asset->cas<ShaderAsset>()->m_name.c_str());
        } else {
            pimpl->m_instancedPassInfo.ubo = std::make_unique<Buffer>(system);
            pimpl->m_instancedPassInfo.ubo->Create(Buffer::BufferType::Uniform, ubo_size);
        }
        vk::DescriptorSetAllocateInfo dsai {
            system.GetGlobalConstantDescriptorPool().get(),
            { pimpl->m_passInfo.desc_layout.get() }
        };
        pimpl->m_instancedPassInfo.desc_set = system.getDevice().allocateDescriptorSets(dsai)[0];
    }

    ComputeStage::~ComputeStage() = default;

    void ComputeStage::SetInBlockVariable(uint32_t index, std::any var)
    {
        pimpl->m_inblock_variables[index] = var;
    }

    void ComputeStage::SetDescVariable(uint32_t index, std::any var)
    {
        pimpl->m_desc_variables[index] = var;
    }

    std::optional<std::pair<uint32_t, bool>> ComputeStage::GetVariableIndex(const std::string &name) const noexcept
    {
        auto itr = pimpl->m_passInfo.inblock.names.find(name);
        if (itr == pimpl->m_passInfo.inblock.names.end()) {
            auto nitr = pimpl->m_passInfo.desc.names.find(name);
            return nitr == pimpl->m_passInfo.desc.names.end() ? 
                std::nullopt : 
                std::optional<std::pair<uint32_t, bool>>{std::make_pair(nitr->second, false)};
        }
        return std::make_pair(itr->second, true);
    }

    void ComputeStage::WriteDescriptorSet()
    {
        auto & pass_info = pimpl->m_instancedPassInfo;

        if (!pass_info.desc_set)    return;
        if (!pass_info.is_descriptor_set_dirty)    return;

        // Prepare descriptor writes
        std::vector <vk::WriteDescriptorSet> writes;

        auto image_writes = PipelineInfo::GetDescriptorImageInfo(
            pimpl->m_desc_variables,
            pimpl->m_passInfo,
            nullptr
        );
        writes.reserve(image_writes.size() + 1);
        for (const auto & [binding, image_info] : image_writes) {
            vk::WriteDescriptorSet write {
                    pass_info.desc_set, binding, 0, 1,
                    // TODO: We need a better way to check if its storage image or texture image.
                    image_info.imageLayout == vk::ImageLayout::eGeneral ? 
                        vk::DescriptorType::eStorageImage : 
                        vk::DescriptorType::eCombinedImageSampler,
                    &image_info, nullptr, nullptr
            };
            writes.push_back(write);
        }

        // write ubo descriptors
        if (pass_info.ubo) {
            auto & ubo = *(pass_info.ubo.get());
            std::array <vk::DescriptorBufferInfo, 1> ubo_buffer_info = {
                vk::DescriptorBufferInfo{ubo.GetBuffer(), 0, vk::WholeSize}
            };
            vk::WriteDescriptorSet ubo_write {
                pass_info.desc_set, 0, 0,
                vk::DescriptorType::eUniformBuffer,
                {},
                ubo_buffer_info,
                {}
            };
            writes.push_back(ubo_write);
        }

        m_system.getDevice().updateDescriptorSets(writes, {});
        pass_info.is_descriptor_set_dirty = false;
    }

    void ComputeStage::WriteUBO()
    {
        if (!pimpl->m_instancedPassInfo.ubo)    return;
        if (!pimpl->m_instancedPassInfo.is_ubo_dirty)    return;

        // write uniform buffer
        auto & ubo = *(pimpl->m_instancedPassInfo.ubo.get());
        PipelineInfo::PlaceUBOVariables(pimpl->m_inblock_variables, pimpl->m_passInfo, pimpl->m_ubo_staging_buffer);
        std::memcpy(ubo.Map(), pimpl->m_ubo_staging_buffer.data(), ubo.GetSize());
        ubo.Flush();

        pimpl->m_instancedPassInfo.is_ubo_dirty = false;
    }

    vk::Pipeline ComputeStage::GetPipeline() const noexcept
    {
        return pimpl->m_passInfo.pipeline.get();
    }
    vk::PipelineLayout ComputeStage::GetPipelineLayout() const noexcept
    {
        return pimpl->m_passInfo.pipeline_layout.get();
    }
    vk::DescriptorSetLayout ComputeStage::GetDescriptorSetLayout() const noexcept
    {
        return pimpl->m_passInfo.desc_layout.get();
    }
    vk::DescriptorSet ComputeStage::GetDescriptorSet() const noexcept
    {
        return pimpl->m_instancedPassInfo.desc_set;
    }
}
