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
            struct BlockLocation {
                uint32_t set;
                uint32_t binding;
            } block_location;

            struct InBlockLocation {
                uint32_t offset;
                uint32_t abs_offset;
                uint32_t size;
            } inblock_location;
            
            using Type = PipelineInfo::ShaderVariable::InBlockVarType;
            Type type;
        };

        struct DesciptorVariableData {
            uint32_t set;
            uint32_t binding;

            using Type = PipelineInfo::ShaderVariable::Type;
            Type type;
        };

        struct ReflectedDataCollection {
            bool has_material_descriptor_set;
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
