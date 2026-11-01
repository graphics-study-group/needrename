#include <cassert>
#include <format>
#include <iostream>

#include "Render/Pipeline/Renderer/VertexAttribute.h"

using namespace Engine;

void Print(const std::vector<Rhi::VertexAttributeDescription> &desc) {
    for (size_t i = 0; i < desc.size(); i++) {
        const auto &d = desc[i];
        std::cout << std::format(
            "Decription {}\n"
            "\tlocation\t{}\n"
            "\tbinding\t\t{}\n"
            "\tformat\t\t{}\n"
            "\toffset\t\t{}",
            i,
            d.location,
            d.binding,
            static_cast<uint32_t>(d.format),
            d.offset
        ) << std::endl;
    }
}

void Print(const std::vector<Rhi::VertexBinding> &desc) {
    for (size_t i = 0; i < desc.size(); i++) {
        const auto &d = desc[i];
        std::cout << std::format(
            "Description {}\n"
            "\tbinding\t\t{}\n"
            "\tstride\t\t{}\n"
            "\trate\t\t{}",
            i,
            static_cast<uint32_t>(d.per_instance ? 1 : 0),
            d.stride,
            d.per_instance ? "instance" : "vertex"
        ) << std::endl;
    }
}

int main() {
    VertexAttribute attribute{};
    attribute.SetAttribute(VertexAttributeSemantic::Position, VertexAttributeType::SFloat32x3)
        .SetAttribute(VertexAttributeSemantic::BoneIndices, VertexAttributeType::Uint16x2)
        .SetAttribute(VertexAttributeSemantic::BoneWeights, VertexAttributeType::SFloat32x2);

    assert(attribute.GetAttribute(VertexAttributeSemantic::Position) == VertexAttributeType::SFloat32x3);
    assert(attribute.GetAttribute(VertexAttributeSemantic::BoneIndices) == VertexAttributeType::Uint16x2);
    assert(attribute.GetAttribute(VertexAttributeSemantic::BoneWeights) == VertexAttributeType::SFloat32x2);

    Print(attribute.ToVertexInputBinding());
    Print(attribute.ToVertexInputAttribute());

    std::cout << "Total size: " << attribute.GetTotalPerVertexSize() << std::endl;
    auto offsets = attribute.EnumerateOffsetFactor();

    std::cout << "Offsets:";
    for (auto offset : offsets) {
        std::cout << " " << offset;
    }
    std::cout << std::endl;
}



