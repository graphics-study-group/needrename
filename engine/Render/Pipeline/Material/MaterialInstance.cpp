#include "MaterialInstance.h"

#include "Asset/Material/MaterialAsset.h"

#include "Render/Memory/SampledTextureInstantiated.h"
#include "Render/Pipeline/PipelineUtils.h"
#include "Render/RenderSystem.h"
#include "Render/RenderSystem/FrameManager.h"
#include "Render/RenderSystem/SubmissionHelper.h"
#include <Asset/Material/MaterialAsset.h>
#include <Asset/Texture/Image2DTextureAsset.h>
#include <Render/Memory/IndexedBuffer.h>
#include <SDL3/SDL.h>
#include <bitset>

namespace Engine {

    struct MaterialInstance::impl {
        struct PassInfo {
            static constexpr uint32_t BACK_BUFFERS = 2;

            std::unique_ptr<IndexedBuffer> ubo{};
            std::array<vk::DescriptorSet, BACK_BUFFERS> desc_set{};

            std::bitset<BACK_BUFFERS> _is_descriptor_set_dirty{};
            std::bitset<BACK_BUFFERS> _is_ubo_dirty{};
        };

        std::weak_ptr<MaterialTemplate> m_parent_template;
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::any>> m_desc_variables{};
        std::unordered_map<uint32_t, std::unordered_map<uint32_t, std::any>> m_inblock_variables{};
        std::unordered_map<uint32_t, PassInfo> m_pass_info{};

        // A small buffer for uniform buffer staging to avoid random write to UBO.
        std::vector<std::byte> m_buffer{};
    };

    MaterialInstance::MaterialInstance(RenderSystem &system, std::shared_ptr<MaterialTemplate> tpl) :
        m_system(system), pimpl(std::make_unique<impl>(tpl)) {
        using PassInfo = impl::PassInfo;
        // Allocate uniform buffers and per-material descriptor sets
        for (const auto &[idx, info] : tpl->GetAllPassInfo()) {
            PassInfo pass{};
            auto ubo_size = tpl->GetMaximalUBOSize(idx);
            pass.desc_set[0] = tpl->AllocateDescriptorSet(idx);
            pass.desc_set[1] = tpl->AllocateDescriptorSet(idx);
            if (ubo_size == 0) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER, "Found zero-sized UBO when processing pass %llu of material", ubo_size
                );
            } else {
                pass.ubo = std::make_unique<IndexedBuffer>(system);
                pass.ubo->Create(
                    Buffer::BufferType::Uniform,
                    tpl->GetMaximalUBOSize(idx),
                    system.GetPhysicalDevice().getProperties().limits.minUniformBufferOffsetAlignment,
                    PassInfo::BACK_BUFFERS,
                    "Indexed UBO for Material"
                );
            }
            pimpl->m_pass_info[idx] = std::move(pass);
        }
    }

    MaterialInstance::~MaterialInstance() = default;

    const MaterialTemplate &MaterialInstance::GetTemplate() const {
        return *(pimpl->m_parent_template.lock());
    }

    const std::unordered_map<uint32_t, std::any> &MaterialInstance::GetInBlockVariables(uint32_t pass_index) const {
        return pimpl->m_inblock_variables.at(pass_index);
    }

    const std::unordered_map<uint32_t, std::any> &MaterialInstance::GetDescVariables(uint32_t pass_index) const {
        return pimpl->m_desc_variables.at(pass_index);
    }

    void MaterialInstance::WriteTextureUniform(
        uint32_t pass, uint32_t index, std::shared_ptr<const SampledTexture> texture
    ) {
        assert(pimpl->m_pass_info.contains(pass) && "Cannot find pass.");
        assert(
            pimpl->m_parent_template.lock()->GetDescVariable(index, pass).set
            && "Cannot find uniform in designated pass."
        );

        pimpl->m_desc_variables[pass][index] = std::any(texture);
        pimpl->m_pass_info[pass]._is_descriptor_set_dirty.set();
    }

    void MaterialInstance::WriteStorageBufferUniform(
        uint32_t pass, uint32_t index, std::shared_ptr<const Buffer> buffer
    ) {
        assert(!"Unimplemented");
    }

    void MaterialInstance::WriteUBOUniform(uint32_t pass, uint32_t index, std::any uniform) {
        assert(
            PipelineUtils::REGISTERED_SHADER_UNIFORM_TYPES.find(uniform.type())
                != PipelineUtils::REGISTERED_SHADER_UNIFORM_TYPES.end()
            && "Unsupported uniform type."
        );
        assert(pimpl->m_pass_info.find(pass) != pimpl->m_pass_info.end() && "Cannot find pass.");
        assert(
            pimpl->m_parent_template.lock()->GetInBlockVariable(index, pass).block_location.set
            && "Cannot find uniform in designated pass."
        );

        pimpl->m_inblock_variables[pass][index] = uniform;
        pimpl->m_pass_info[pass]._is_ubo_dirty.set();
    }

    void MaterialInstance::WriteUBO(uint32_t pass) {
        assert(pimpl->m_pass_info.contains(pass) && "Cannot find pass.");
        auto &pass_info = pimpl->m_pass_info[pass];
        auto fif = m_system.GetFrameManager().GetTotalFrame() % pass_info.BACK_BUFFERS;
        if (!pass_info._is_ubo_dirty[fif]) return;

        auto tpl = pimpl->m_parent_template.lock();

        // write uniform buffer
        auto &ubo = *(pass_info.ubo.get());
        tpl->PlaceUBOVariables(*this, pimpl->m_buffer, pass);
        std::memcpy(ubo.GetSlicePtr(fif), pimpl->m_buffer.data(), ubo.GetSliceSize());
        ubo.FlushSlice(fif);

        pass_info._is_ubo_dirty.reset(fif);
    }

    void MaterialInstance::WriteDescriptors(uint32_t pass) {
        assert(pimpl->m_pass_info.contains(pass) && "Cannot find pass.");
        auto &pass_info = pimpl->m_pass_info[pass];
        auto fif = m_system.GetFrameManager().GetTotalFrame() % pass_info.BACK_BUFFERS;
        if (!(pass_info.desc_set[fif])) return;
        if (!pass_info._is_descriptor_set_dirty[fif]) return;

        auto tpl = pimpl->m_parent_template.lock();

        // Prepare descriptor writes
        std::vector<vk::WriteDescriptorSet> writes;

        auto image_writes = tpl->GetDescriptorImageInfo(*this, pass);
        writes.reserve(image_writes.size() + 1);
        for (const auto &[binding, image_info] : image_writes) {
            vk::WriteDescriptorSet write{
                pass_info.desc_set[fif],
                binding,
                0,
                1,
                // TODO: We need a better way to check if its storage image or texture image.
                image_info.imageLayout == vk::ImageLayout::eGeneral ? vk::DescriptorType::eStorageImage
                                                                    : vk::DescriptorType::eCombinedImageSampler,
                &image_info,
                nullptr,
                nullptr
            };
            writes.push_back(write);
        }

        // write ubo descriptors
        if (pass_info.ubo) {
            auto &ubo = *(pass_info.ubo.get());
            std::array<vk::DescriptorBufferInfo, 1> ubo_buffer_info = {
                vk::DescriptorBufferInfo{ubo.GetBuffer(), ubo.GetSliceOffset(fif), ubo.GetSliceSize()}
            };
            vk::WriteDescriptorSet ubo_write{
                pass_info.desc_set[fif], 0, 0, vk::DescriptorType::eUniformBuffer, {}, ubo_buffer_info, {}
            };
            writes.push_back(ubo_write);
        }

        m_system.getDevice().updateDescriptorSets(writes, {});
        pass_info._is_descriptor_set_dirty.reset(fif);
    }
    vk::DescriptorSet MaterialInstance::GetDescriptor(uint32_t pass) const {
        return GetDescriptor(pass, m_system.GetFrameManager().GetTotalFrame() % impl::PassInfo::BACK_BUFFERS);
    }
    vk::DescriptorSet MaterialInstance::GetDescriptor(uint32_t pass, uint32_t backbuffer) const {
        assert(backbuffer < impl::PassInfo::BACK_BUFFERS);
        return pimpl->m_pass_info.at(pass).desc_set[backbuffer];
    }
    void MaterialInstance::Instantiate(const MaterialAsset &asset) {
        const auto &all_passes = pimpl->m_parent_template.lock()->GetAllPassInfo();
        for (const auto &[pass_index, pass_info] : all_passes) {
            for (const auto &[uniform_name, uniform_idx] : pass_info.inblock.names) {
                auto itr = asset.m_properties.find(uniform_name);
                if (itr == asset.m_properties.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "In-block variable %s has no value.", uniform_name.c_str());
                    continue;
                }

                auto &uniform = pass_info.inblock.vars[uniform_idx];
                assert(
                    itr->second.m_type == MaterialProperty::Type::Undefined
                    && "A in-block variable has wrong descriptor type."
                );

                switch (uniform.type) {
                case ShaderUtils::InBlockVariableData::Type::Int:
                    assert(itr->second.m_ubo_type == ShaderUtils::InBlockVariableData::Type::Int);
                    this->WriteUBOUniform(pass_index, uniform_idx, std::any_cast<int>(itr->second.m_value));
                    break;
                case ShaderUtils::InBlockVariableData::Type::Float:
                    assert(itr->second.m_ubo_type == ShaderUtils::InBlockVariableData::Type::Float);
                    this->WriteUBOUniform(pass_index, uniform_idx, std::any_cast<float>(itr->second.m_value));
                    break;
                case ShaderUtils::InBlockVariableData::Type::Vec4:
                    assert(itr->second.m_ubo_type == ShaderUtils::InBlockVariableData::Type::Vec4);
                    this->WriteUBOUniform(pass_index, uniform_idx, std::any_cast<glm::vec4>(itr->second.m_value));
                    break;
                case ShaderUtils::InBlockVariableData::Type::Mat4:
                    assert(itr->second.m_ubo_type == ShaderUtils::InBlockVariableData::Type::Mat4);
                    this->WriteUBOUniform(pass_index, uniform_idx, std::any_cast<glm::vec4>(itr->second.m_value));
                    break;
                default:
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Unidentified in-block variable type.");
                }
            }

            for (const auto &[desc_name, desc_idx] : pass_info.desc.names) {
                auto itr = asset.m_properties.find(desc_name);
                if (itr == asset.m_properties.end()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Descriptor variable %s has no value.", desc_name.c_str());
                    continue;
                }

                auto &descvar = pass_info.desc.vars[desc_idx];
                assert(
                    itr->second.m_ubo_type == MaterialProperty::InBlockVarType::Undefined
                    && "A descriptor variable has wrong in-block type."
                );

                switch (descvar.type) {
                case ShaderUtils::DesciptorVariableData::Type::UBO:
                    break;
                case ShaderUtils::DesciptorVariableData::Type::Texture: {
                    assert(itr->second.m_type == MaterialProperty::Type::Texture);
                    auto texture_asset =
                        std::any_cast<std::shared_ptr<AssetRef>>(itr->second.m_value)->as<Image2DTextureAsset>();
                    // TODO: We should allocate texture from assets in a pool.
                    auto texture = std::make_shared<SampledTextureInstantiated>(m_system);
                    texture->Instantiate(*texture_asset);
                    this->WriteTextureUniform(pass_index, desc_idx, texture);
                    m_system.GetFrameManager().GetSubmissionHelper().EnqueueTextureBufferSubmission(
                        *texture, texture_asset->GetPixelData(), texture_asset->GetPixelDataSize()
                    );
                    break;
                }
                default:
                    SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "Unidentified descriptor variable type.");
                }
            }
        }
    }
} // namespace Engine
