#ifndef RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED
#define RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED

#include <vulkan/vulkan.hpp>
#include <Asset/Material/MaterialTemplateAsset.h>

namespace Engine{
    namespace MaterialTemplateUtils{
        using FillingMode = MaterialTemplateSinglePassProperties::RasterizerProperties::FillingMode;
        using CullingMode = MaterialTemplateSinglePassProperties::RasterizerProperties::CullingMode;
        using FrontFace = MaterialTemplateSinglePassProperties::RasterizerProperties::FrontFace;

        vk::PolygonMode ToVkPolygonMode(FillingMode mode);
        vk::CullModeFlags ToVkCullMode(CullingMode mode);
        vk::FrontFace ToVkFrontFace(FrontFace face);

        vk::PipelineRasterizationStateCreateInfo
        ToVulkanRasterizationStateCreateInfo(MaterialTemplateSinglePassProperties::RasterizerProperties);
    }
}

#endif // RENDER_MATERIAL_MATERIALTEMPLATEUTILS_INCLUDED
