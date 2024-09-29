#include "PipelineConfigurationStruct.h"

namespace Engine {
    vk::PolygonMode RasterizationConfig::ToVkPolygonMode(FillingMode mode)
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
    vk::CullModeFlags RasterizationConfig::ToVkCullMode(CullingMode mode)
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
    vk::FrontFace RasterizationConfig::ToVkFrontFace(FrontFace face)
    {
        switch (face){
        case FrontFace::Counterclockwise:
            return vk::FrontFace::eCounterClockwise;
        case FrontFace::Clockwise:
            return vk::FrontFace::eClockwise;
        }
        __builtin_unreachable();
    }

    vk::PipelineRasterizationStateCreateInfo RasterizationConfig::ToVulkanRasterizationStateCreateInfo() const
    {
        vk::PipelineRasterizationStateCreateInfo info{};
        info.depthClampEnable = vk::False;
        info.rasterizerDiscardEnable = vk::False;
        info.polygonMode = ToVkPolygonMode(this->filling);
        info.lineWidth = this->line_width;
        info.cullMode = ToVkCullMode(this->culling);
        info.frontFace = ToVkFrontFace(this->front);
        info.depthBiasEnable = vk::False;
        return info;
    }
};
