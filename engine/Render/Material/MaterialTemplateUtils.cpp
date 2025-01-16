#include "MaterialTemplateUtils.h"

namespace Engine {
    namespace MaterialTemplateUtils {
        vk::PolygonMode ToVkPolygonMode(MaterialTemplateSinglePassProperties::RasterizerProperties::FillingMode mode)
        {
            
            switch (mode) {
            case FillingMode::Fill:
                return vk::PolygonMode::eFill;
            case FillingMode::Line:
                return vk::PolygonMode::eLine;
            case FillingMode::Point:
                return vk::PolygonMode::ePoint;
            }
            __builtin_unreachable();
        }
        vk::CullModeFlags ToVkCullMode(MaterialTemplateSinglePassProperties::RasterizerProperties::CullingMode mode)
        {
            
            switch (mode) {
            case CullingMode::None:
                return vk::CullModeFlagBits::eNone;
            case CullingMode::Front:
                return vk::CullModeFlagBits::eFront;
            case CullingMode::Back:
                return vk::CullModeFlagBits::eBack;
            case CullingMode::All:
                return vk::CullModeFlagBits::eFrontAndBack;
            }
            __builtin_unreachable();
        }
        vk::FrontFace ToVkFrontFace(MaterialTemplateSinglePassProperties::RasterizerProperties::FrontFace face)
        {
            switch (face){
            case FrontFace::Counterclockwise:
                return vk::FrontFace::eCounterClockwise;
            case FrontFace::Clockwise:
                return vk::FrontFace::eClockwise;
            }
            __builtin_unreachable();
        }

        vk::PipelineRasterizationStateCreateInfo ToVulkanRasterizationStateCreateInfo(MaterialTemplateSinglePassProperties::RasterizerProperties prop)
        {
            vk::PipelineRasterizationStateCreateInfo info{};
            info.depthClampEnable = vk::False;
            info.rasterizerDiscardEnable = vk::False;
            info.polygonMode = ToVkPolygonMode(prop.filling);
            info.lineWidth = prop.line_width;
            info.cullMode = ToVkCullMode(prop.culling);
            info.frontFace = ToVkFrontFace(prop.front);
            info.depthBiasEnable = vk::False;
            return info;
        }
    }
};
