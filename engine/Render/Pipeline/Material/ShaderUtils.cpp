#include "ShaderUtils.h"

#include <spirv_reflect.h>
#include <SDL3/SDL.h>

namespace Engine
{
    namespace ShaderUtils
    {
        ReflectedDataCollection ReflectSpirvData(const std::vector<uint32_t> &spirv_code)
        {
            // Automatically deconstructed, so we don't need deallocation.
            spv_reflect::ShaderModule shaderModule{spirv_code};

            uint32_t descriptor_set_count;
            auto result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, nullptr);
            assert(result == SPV_REFLECT_RESULT_SUCCESS);
            std::vector<SpvReflectDescriptorSet *> descriptor_sets{descriptor_set_count, nullptr};
            result = shaderModule.EnumerateDescriptorSets(&descriptor_set_count, descriptor_sets.data());

            DescriptorSetLayoutData per_material_descriptor_set_layout{};
            for (size_t i_set = 0; i_set < descriptor_sets.size(); ++i_set)
            {
                const SpvReflectDescriptorSet &refl_set = *(descriptor_sets[i_set]);
                DescriptorSetLayoutData * layout = nullptr;

                if (refl_set.set == 2) {
                    layout = &per_material_descriptor_set_layout;
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
                }
                layout->set_number = refl_set.set;
                layout->create_info.bindingCount = refl_set.binding_count;
                layout->create_info.pBindings = layout->bindings.data();
            }

            assert(per_material_descriptor_set_layout.set_number == 2);
            return ReflectedDataCollection{
                .per_material_descriptor_set_layout = per_material_descriptor_set_layout
            };
        }
    }
} // namespace Engine
