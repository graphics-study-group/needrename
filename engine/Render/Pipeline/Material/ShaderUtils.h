#ifndef PIPELINE_MATERIAL_SHADERUTILS_INCLUDED
#define PIPELINE_MATERIAL_SHADERUTILS_INCLUDED

#include "Asset/Material/ShaderAsset.h"
#include <unordered_map>
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
            
            using Type = ShaderInBlockVariableProperty::InBlockVarType;
            Type type;
        };

        struct DesciptorVariableData {
            uint32_t set;
            uint32_t binding;

            using Type = ShaderVariableProperty::Type;
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

        struct ReflectedDataCollectionCompute {
            DescriptorSetLayoutData set_layout;

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
        ReflectedDataCollectionCompute ReflectSpirvDataCompute(const std::vector <uint32_t> & spirv_code);
    }
} // namespace Engine


#endif // PIPELINE_MATERIAL_SHADERUTILS_INCLUDED
