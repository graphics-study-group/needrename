#include "ShaderUtils.h"

#include <spirv_reflect.h>
#include <SDL3/SDL.h>

namespace Engine
{
    namespace ShaderUtils
    {
        std::vector<std::pair <std::string, InBlockVariableData>> 
        EnumerateInBlockVariables (const SpvReflectDescriptorBinding & desc_binding) {
            assert(desc_binding.block.member_count > 0);
            std::vector<std::pair <std::string, InBlockVariableData>> ret{desc_binding.block.member_count};
            for (uint32_t i = 0; i < desc_binding.block.member_count; i++) {
                const auto & member = desc_binding.block.members[i];
                assert(member.member_count == 0 && "Inner block members not supported");

                ret[i].first = member.name;
                auto & data = ret[i].second;
                data.inblock_location = {
                    .offset = member.offset,
                    .abs_offset = member.absolute_offset,
                    .size = member.size
                };
                data.block_location = {
                    .set = desc_binding.set,
                    .binding = desc_binding.binding
                };

                const auto & type_desc = *(member.type_description);
                if (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR) {
                    assert((type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) && "Only vec4 vector type is supported.");
                    assert(type_desc.traits.numeric.vector.component_count == 4 && "Only vec4 vector type is supported.");
                    data.type = InBlockVariableData::Type::Vec4;
                }
                else if (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX) {
                    assert((type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) && "Only mat4 matrix type is supported.");
                    assert(type_desc.traits.numeric.matrix.column_count == 4 
                        && type_desc.traits.numeric.matrix.row_count == 4 
                        && "Only mat4 matrix type is supported."
                    );
                    data.type = InBlockVariableData::Type::Mat4;
                }
            }
            return ret;
        }

        ReflectedDataCollection ReflectSpirvData(const std::vector<uint32_t> &spirv_code)
        {
            // Automatically deconstructed, so we don't need deallocation.
            spv_reflect::ShaderModule shaderModule{spirv_code};

            uint32_t descriptor_set_count;
            auto result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, nullptr);
            assert(result == SPV_REFLECT_RESULT_SUCCESS);
            std::vector<SpvReflectDescriptorSet *> descriptor_sets{descriptor_set_count, nullptr};
            result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, descriptor_sets.data());
            assert(result == SPV_REFLECT_RESULT_SUCCESS);

            ReflectedDataCollection reflected{};
            for (size_t i_set = 0; i_set < descriptor_sets.size(); ++i_set)
            {
                const SpvReflectDescriptorSet &refl_set = *(descriptor_sets[i_set]);
                DescriptorSetLayoutData * layout = nullptr;

                if (refl_set.set == 2) {
                    reflected.has_material_descriptor_set = true;
                    layout = &reflected.per_material_descriptor_set_layout;
                } else {
                    continue;
                }

                layout->bindings.resize(refl_set.binding_count);
                for (uint32_t i_binding = 0; i_binding < refl_set.binding_count; ++i_binding)
                {
                    const SpvReflectDescriptorBinding &refl_binding = *(refl_set.bindings[i_binding]);
                    vk::DescriptorSetLayoutBinding &layout_binding = layout->bindings[i_binding];
                    layout_binding.binding = refl_binding.binding;
                    layout_binding.descriptorType = static_cast<vk::DescriptorType>(
                        static_cast<VkDescriptorType>(refl_binding.descriptor_type)
                    );
                    layout_binding.descriptorCount = 1;
                    for (uint32_t i_dim = 0; i_dim < refl_binding.array.dims_count; ++i_dim)
                    {
                        layout_binding.descriptorCount *= refl_binding.array.dims[i_dim];
                    }
                    layout_binding.stageFlags = static_cast<vk::ShaderStageFlagBits>(
                        static_cast<VkShaderStageFlagBits>(shaderModule.GetShaderStage())
                    );

                    switch(layout_binding.descriptorType) {
                        // UBO or SSBO
                        case vk::DescriptorType::eUniformBuffer:
                            reflected.desc.names[refl_binding.name] = reflected.desc.vars.size();
                            reflected.desc.vars.push_back(DesciptorVariableData{
                                .set = refl_set.set,
                                .binding = refl_binding.binding,
                                .type = DesciptorVariableData::Type::UBO
                            });
                            for (const auto & [name, type] : EnumerateInBlockVariables(refl_binding)) {
                                reflected.inblock.names[name] = reflected.inblock.vars.size();
                                reflected.inblock.vars.push_back(std::move(type));
                            }
                            break;
                        case vk::DescriptorType::eStorageBuffer:
                            reflected.desc.names[refl_binding.name] = reflected.desc.vars.size();
                            reflected.desc.vars.push_back(DesciptorVariableData{
                                .set = refl_set.set,
                                .binding = refl_binding.binding,
                                .type = DesciptorVariableData::Type::StorageBuffer
                            });
                            break;
                        case vk::DescriptorType::eStorageImage:
                            reflected.desc.names[refl_binding.name] = reflected.desc.vars.size();
                            reflected.desc.vars.push_back(DesciptorVariableData{
                                .set = refl_set.set,
                                .binding = refl_binding.binding,
                                .type = DesciptorVariableData::Type::StorageImage
                            });
                            break;
                        case vk::DescriptorType::eCombinedImageSampler:
                            reflected.desc.names[refl_binding.name] = reflected.desc.vars.size();
                            reflected.desc.vars.push_back(DesciptorVariableData{
                                .set = refl_set.set,
                                .binding = refl_binding.binding,
                                .type = DesciptorVariableData::Type::Texture
                            });
                            break;
                        default:
                            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Unexpected descriptor type: %u", static_cast<uint32_t>(layout_binding.descriptorType));
                    };
                }
                layout->set_number = refl_set.set;
                layout->create_info.bindingCount = refl_set.binding_count;
                layout->create_info.pBindings = layout->bindings.data();
            }

            return reflected;
        }
    }
} // namespace Engine
