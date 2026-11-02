#include "VertexAttribute.h"

#include "Rhi/Device/AllocatorState.h"

namespace {
    constexpr Engine::Rhi::VertexFormat ToVertexFormat(Engine::VertexAttributeType type) {
        switch (type) {
            using enum Engine::VertexAttributeType;
        case SFloat32x1:
            return Engine::Rhi::VertexFormat::Float32x1;
        case SFloat32x2:
            return Engine::Rhi::VertexFormat::Float32x2;
        case SFloat32x3:
            return Engine::Rhi::VertexFormat::Float32x3;
        case SFloat32x4:
            return Engine::Rhi::VertexFormat::Float32x4;
        case Uint8x4:
            return Engine::Rhi::VertexFormat::UInt8x4;
        case Uint16x2:
            return Engine::Rhi::VertexFormat::UInt16x2;
        case Uint32x1:
            return Engine::Rhi::VertexFormat::UInt32x1;
        default:
            return Engine::Rhi::VertexFormat::Undefined;
        }
    }

    constexpr Engine::Rhi::ImageFormat ToImageFormat(Engine::Rhi::VertexFormat format) {
        switch (format) {
            using enum Engine::Rhi::VertexFormat;
        case Float32x1:
            return Engine::Rhi::ImageFormat::R32SFloat;
        case Float32x2:
            return Engine::Rhi::ImageFormat::R32G32SFloat;
        case Float32x3:
            return Engine::Rhi::ImageFormat::R32G32B32SFloat;
        case UInt8x4:
            return Engine::Rhi::ImageFormat::R8G8B8A8UInt;
        case UInt16x2:
            return Engine::Rhi::ImageFormat::R16G16UInt;
        case UInt32x1:
            return Engine::Rhi::ImageFormat::R32UInt;
        default:
            return Engine::Rhi::ImageFormat::UNDEFINED;
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
} // namespace

namespace Engine {
    std::vector<Rhi::VertexBinding> VertexAttribute::ToVertexInputBinding() const noexcept {
        std::vector<Rhi::VertexBinding> ret;
        ret.reserve(16);

        for (uint32_t i = 0; i < 16; i++) {
            auto type = this->GetAttribute(static_cast<VertexAttributeSemantic>(i));
            auto stride = GetStride(type);
            if (stride == 0) continue;

            // alignment is required only if VK_KHR_portability_subset is enabled.
            ret.push_back(
                Rhi::VertexBinding{
                    stride, false
                }
            );
        }

        ret.shrink_to_fit();
        return ret;
    }

    std::vector<Rhi::VertexAttributeDescription> VertexAttribute::ToVertexInputAttribute(
        Rhi::AllocatorState *allocator
    ) const noexcept {
        std::vector<Rhi::VertexAttributeDescription> ret;
        ret.reserve(16);

        for (uint32_t i = 0; i < 16; i++) {
            auto type = this->GetAttribute(static_cast<VertexAttributeSemantic>(i));
            const auto format = ToVertexFormat(type);
            if (format == Rhi::VertexFormat::Undefined) continue;

            if (allocator) {
                assert(
                    allocator->QueryFormatFeatures(ToImageFormat(format), Rhi::FormatFeature::VertexBuffer)
                    && "Format not supported as vertex attribute buffer."
                );
            }

            ret.push_back(Rhi::VertexAttributeDescription{i, static_cast<uint32_t>(ret.size()), format, 0u});
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
        std::vector<uint64_t> ret;
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




