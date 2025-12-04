#include "VertexAttribute.h"

#include <vulkan/vulkan.hpp>

namespace {
    constexpr vk::Format ToVkFormat(Engine::VertexAttributeType type) {
        switch (type) {
            using enum Engine::VertexAttributeType;
            case SFloat32x1:
                return vk::Format::eR32Sfloat;
            case SFloat32x2:
                return vk::Format::eR32G32Sfloat;
            case SFloat32x3:
                return vk::Format::eR32G32B32Sfloat;
            case SFloat32x4:
                return vk::Format::eR32G32B32A32Sfloat;
            case Uint8x4:
                return vk::Format::eR8G8B8A8Uint;
            case Uint16x2:
                return vk::Format::eR16G16Uint;
            case Uint32x1:
                return vk::Format::eR32Uint;
            default:
                return vk::Format::eUndefined;
        }
    }

    constexpr uint32_t GetSize(Engine::VertexAttributeType type) {
        switch (type) {
            using enum Engine::VertexAttributeType;
            case SFloat32x1:
                return sizeof(float);
            case SFloat32x2:
                return sizeof(float) * 2;
            case SFloat32x3:
                return sizeof(float) * 3;
            case SFloat32x4:
                return sizeof(float) * 4;
            case Uint8x4:
                return sizeof(uint8_t) * 4;
            case Uint16x2:
                return sizeof(uint16_t) * 2;
            case Uint32x1:
                return sizeof(uint32_t) * 1;
            default:
                return 0;
        }
    }

    constexpr uint32_t GetStride(Engine::VertexAttributeType type) {
        return GetSize(type);
    }
}

namespace Engine {
    std::vector<vk::VertexInputBindingDescription> VertexAttribute::ToVkVertexInputBinding() const noexcept {
        std::vector<vk::VertexInputBindingDescription> ret;
        ret.reserve(16);

        for (uint32_t i = 0; i < 16; i++) {
            auto type = this->GetAttribute(static_cast<VertexAttributeSemantic>(i));
            auto stride = GetStride(type);
            if (stride == 0)   continue;
            
            // alignment is required only if VK_KHR_portability_subset is enabled.
            ret.push_back(vk::VertexInputBindingDescription{
                static_cast<uint32_t>(ret.size()),
                stride,
                vk::VertexInputRate::eVertex
            });
        }

        ret.shrink_to_fit();
        return ret;
    }

    std::vector<vk::VertexInputAttributeDescription> VertexAttribute::ToVkVertexAttribute(
        RenderSystemState::AllocatorState *allocator
    ) const noexcept {
        std::vector <vk::VertexInputAttributeDescription> ret;
        ret.reserve(16);

        for (uint32_t i = 0; i < 16; i++) {
            auto type = this->GetAttribute(static_cast<VertexAttributeSemantic>(i));
            auto format = ToVkFormat(type);
            if (format == vk::Format::eUndefined)   continue;

            if (allocator) {
                assert(
                    allocator->QueryFormatFeatures(format, vk::FormatFeatureFlagBits::eVertexBuffer)
                    && "Format not supported as vertex attribute buffer."
                );
            }
            
            ret.push_back(vk::VertexInputAttributeDescription{
                i,
                static_cast<uint32_t>(ret.size()),
                format,
                0u
            });
        }
        
        ret.shrink_to_fit();
        return ret;
    }

    uint32_t VertexAttribute::GetTotalPerVertexSize() const noexcept {
        uint32_t ret{0};
        for (uint32_t i = 0; i < 16; i++) {
            auto type = this->GetAttribute(static_cast<VertexAttributeSemantic>(i));
            auto stride = GetSize(type);
            ret += stride;
        }
        return ret;
    }
    uint32_t VertexAttribute::GetPerVertexSize(VertexAttributeSemantic semantic) const noexcept {
        return GetSize(GetAttribute(semantic));
    }
    uint64_t VertexAttribute::GetOffsetFactor(VertexAttributeSemantic semantic) const noexcept {
        uint32_t ret{0};
        for (uint32_t i = 0; i < static_cast<uint32_t>(semantic); i++) {
            auto type = this->GetAttribute(static_cast<VertexAttributeSemantic>(i));
            auto stride = GetStride(type);
            ret += stride;
        }
        return ret;
    }
    std::vector<uint64_t> VertexAttribute::EnumerateOffsetFactor() const noexcept {
        std::vector <uint64_t> ret;
        uint64_t offset{0};
        ret.reserve(16);
        for (uint32_t i = 0; i < 16; i++) {
            auto type = this->GetAttribute(static_cast<VertexAttributeSemantic>(i));
            auto stride = GetStride(type);
            if (stride) ret.push_back(offset);

            offset += stride;
        }
        return ret;
    }
} // namespace Engine
