#ifndef RENDER_PIPELINE_SHADER_INCLUDED
#define RENDER_PIPELINE_SHADER_INCLUDED

#include "Render/Pipeline/PipelineInfo.h"
#include <vulkan/vulkan.hpp>

namespace Engine
{
    namespace ShaderUtils {
        struct DescriptorSetLayoutData {
            uint32_t set_number;
            vk::DescriptorSetLayoutCreateInfo create_info;
            std::vector<vk::DescriptorSetLayoutBinding> bindings;
        };

        struct InBlockVariableData {
            struct Location{
                uint32_t set;
                uint32_t binding;
            } block_location;
            PipelineInfo::ShaderVariable::UBOType type;
        };

        struct DesciptorVariableData {
            uint32_t set;
            uint32_t binding;
            PipelineInfo::ShaderVariable::Type type;
        };

        struct ReflectedDataCollection {
            DescriptorSetLayoutData per_material_descriptor_set_layout;

            struct {
                std::unordered_map <std::string, uint32_t> names;
                std::vector <InBlockVariableData> vars;
            } inblock;
            
            struct {
                std::unordered_map <std::string, uint32_t> names;
                std::vector <DesciptorVariableData> vars;
            } desc;
        };

        ReflectedDataCollection ReflectSpirvData(const std::vector<uint32_t> & spirv_code);
    }
} // namespace Engine


#endif // RENDER_PIPELINE_SHADER_INCLUDED
