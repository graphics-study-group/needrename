#include "ShaderUtils.h"

#include <SDL3/SDL.h>
#include <spirv_reflect.h>

namespace Engine {
    namespace ShaderUtils {
        std::vector<std::pair<std::string, InBlockVariableData>> EnumerateInBlockVariables(
            const SpvReflectDescriptorBinding &desc_binding
        ) {
            assert(desc_binding.block.member_count > 0);
            std::vector<std::pair<std::string, InBlockVariableData>> ret{desc_binding.block.member_count};
            for (uint32_t i = 0; i < desc_binding.block.member_count; i++) {
                const auto &member = desc_binding.block.members[i];
                assert(member.member_count == 0 && "Inner block members not supported");

                ret[i].first = member.name;
                auto &data = ret[i].second;
                data.inblock_location = {
                    .offset = member.offset, .abs_offset = member.absolute_offset, .size = member.size
                };
                data.block_location = {.set = desc_binding.set, .binding = desc_binding.binding};

                const auto &type_desc = *(member.type_description);
                if (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_VECTOR) {
                    assert(
                        (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) && "Only vec4 vector type is supported."
                    );
                    assert(
                        type_desc.traits.numeric.vector.component_count == 4 && "Only vec4 vector type is supported."
                    );
                    data.type = InBlockVariableData::Type::Vec4;
                } else if (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_MATRIX) {
                    assert(
                        (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) && "Only mat4 matrix type is supported."
                    );
                    assert(
                        type_desc.traits.numeric.matrix.column_count == 4
                        && type_desc.traits.numeric.matrix.row_count == 4 && "Only mat4 matrix type is supported."
                    );
                    data.type = InBlockVariableData::Type::Mat4;
                } else if (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_FLOAT) {
                    assert(
                        type_desc.traits.numeric.scalar.width == 32 && "Only 4-byte single precision float is supported"
                    );
                    data.type = InBlockVariableData::Type::Float;
                } else if (type_desc.type_flags & SPV_REFLECT_TYPE_FLAG_INT) {
                    assert(type_desc.traits.numeric.scalar.width == 32 && "Only 32 bit signed integer is supported");
                    assert(
                        type_desc.traits.numeric.scalar.signedness == 1 && "Only 32 bit signed integer is supported"
                    );
                    data.type = InBlockVariableData::Type::Int;
                }
            }
            return ret;
        }

        void ProcessDescriptors(
            const spv_reflect::ShaderModule &shader,
            const SpvReflectDescriptorSet &refl_set,
            std::unordered_map<std::string, uint32_t> &names,
            std::vector<DesciptorVariableData> &vars,
            std::unordered_map<std::string, uint32_t> &inblock_names,
            std::vector<InBlockVariableData> &inblock_vars,
            std::vector<vk::DescriptorSetLayoutBinding> &bindings
        ) {
            bindings.resize(refl_set.binding_count);
            for (uint32_t i_binding = 0; i_binding < refl_set.binding_count; ++i_binding) {
                const SpvReflectDescriptorBinding &refl_binding = *(refl_set.bindings[i_binding]);
                vk::DescriptorSetLayoutBinding &layout_binding = bindings[i_binding];
                layout_binding.binding = refl_binding.binding;
                layout_binding.descriptorType =
                    static_cast<vk::DescriptorType>(static_cast<VkDescriptorType>(refl_binding.descriptor_type));
                layout_binding.descriptorCount = 1;
                for (uint32_t i_dim = 0; i_dim < refl_binding.array.dims_count; ++i_dim) {
                    layout_binding.descriptorCount *= refl_binding.array.dims[i_dim];
                }
                layout_binding.stageFlags =
                    static_cast<vk::ShaderStageFlagBits>(static_cast<VkShaderStageFlagBits>(shader.GetShaderStage()));

                switch (layout_binding.descriptorType) {
                // UBO or SSBO
                case vk::DescriptorType::eUniformBuffer:
                    names[refl_binding.name] = vars.size();
                    vars.push_back(
                        DesciptorVariableData{
                            .set = refl_set.set,
                            .binding = refl_binding.binding,
                            .type = DesciptorVariableData::Type::UBO
                        }
                    );
                    for (const auto &[name, type] : EnumerateInBlockVariables(refl_binding)) {
                        inblock_names[name] = inblock_vars.size();
                        inblock_vars.push_back(std::move(type));
                    }
                    break;
                case vk::DescriptorType::eStorageBuffer:
                    names[refl_binding.name] = vars.size();
                    vars.push_back(
                        DesciptorVariableData{
                            .set = refl_set.set,
                            .binding = refl_binding.binding,
                            .type = DesciptorVariableData::Type::StorageBuffer
                        }
                    );
                    break;
                case vk::DescriptorType::eStorageImage:
                    names[refl_binding.name] = vars.size();
                    vars.push_back(
                        DesciptorVariableData{
                            .set = refl_set.set,
                            .binding = refl_binding.binding,
                            .type = DesciptorVariableData::Type::StorageImage
                        }
                    );
                    break;
                case vk::DescriptorType::eCombinedImageSampler:
                    names[refl_binding.name] = vars.size();
                    vars.push_back(
                        DesciptorVariableData{
                            .set = refl_set.set,
                            .binding = refl_binding.binding,
                            .type = DesciptorVariableData::Type::Texture
                        }
                    );
                    break;
                default:
                    SDL_LogError(
                        SDL_LOG_CATEGORY_RENDER,
                        "Unexpected descriptor type: %u",
                        static_cast<uint32_t>(layout_binding.descriptorType)
                    );
                };
            }
        }

        ReflectedDataCollection ReflectSpirvData(const std::vector<uint32_t> &spirv_code) {
            // Automatically deconstructed, so we don't need deallocation.
            spv_reflect::ShaderModule shaderModule{spirv_code};
            assert(
                (shaderModule.GetShaderStage() & VK_SHADER_STAGE_ALL_GRAPHICS)
                && "The shader provided is not a graphic pipeline shader."
            );

            uint32_t descriptor_set_count;
            auto result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, nullptr);
            assert(result == SPV_REFLECT_RESULT_SUCCESS);
            std::vector<SpvReflectDescriptorSet *> descriptor_sets{descriptor_set_count, nullptr};
            result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, descriptor_sets.data());
            assert(result == SPV_REFLECT_RESULT_SUCCESS);

            ReflectedDataCollection reflected{};
            for (size_t i_set = 0; i_set < descriptor_sets.size(); ++i_set) {
                const SpvReflectDescriptorSet &refl_set = *(descriptor_sets[i_set]);
                DescriptorSetLayoutData *layout = nullptr;

                if (refl_set.set == 2) {
                    reflected.has_material_descriptor_set = true;
                    layout = &reflected.per_material_descriptor_set_layout;
                } else {
                    continue;
                }

                ProcessDescriptors(
                    shaderModule,
                    refl_set,
                    reflected.desc.names,
                    reflected.desc.vars,
                    reflected.inblock.names,
                    reflected.inblock.vars,
                    layout->bindings
                );
                layout->set_number = refl_set.set;
                layout->create_info.bindingCount = refl_set.binding_count;
                layout->create_info.pBindings = layout->bindings.data();
            }

            return reflected;
        }

        ReflectedDataCollectionCompute ReflectSpirvDataCompute(const std::vector<uint32_t> &spirv_code) {
            spv_reflect::ShaderModule shaderModule{spirv_code};
            assert(
                (shaderModule.GetShaderStage() & VK_SHADER_STAGE_COMPUTE_BIT)
                && "The shader provided is not a compute shader."
            );

            uint32_t descriptor_set_count;
            auto result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, nullptr);
            assert(result == SPV_REFLECT_RESULT_SUCCESS);
            assert(descriptor_set_count == 1 && "Only exactly one descriptor set is supported for compute shader.");
            std::vector<SpvReflectDescriptorSet *> descriptor_sets{descriptor_set_count, nullptr};
            result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, descriptor_sets.data());
            assert(result == SPV_REFLECT_RESULT_SUCCESS);

            ReflectedDataCollectionCompute reflected{};

            const SpvReflectDescriptorSet &refl_set = *(descriptor_sets[0]);
            assert(
                refl_set.set == 0 && "Only exactly one descriptor set numbered zero is supported for compute shader."
            );

            ProcessDescriptors(
                shaderModule,
                refl_set,
                reflected.desc.names,
                reflected.desc.vars,
                reflected.inblock.names,
                reflected.inblock.vars,
                reflected.set_layout.bindings
            );
            reflected.set_layout.set_number = refl_set.set;
            reflected.set_layout.create_info.bindingCount = refl_set.binding_count;
            reflected.set_layout.create_info.pBindings = reflected.set_layout.bindings.data();

            return reflected;
        }
    } // namespace ShaderUtils
} // namespace Engine
