#include <vulkan/vulkan.hpp>
#include <iostream>

#include "Render/Renderer/VertexAttribute.h"

using namespace Engine;

void Print(const std::vector <vk::VertexInputAttributeDescription> & desc) {
    for (size_t i = 0; i < desc.size(); i++) {
        const auto & d = desc[i];
        std::cout << std::format(
            "Decription {}\n"
            "\tlocation\t{}\n"
            "\tbinding\t\t{}\n"
            "\tformat\t\t{}\n"
            "\toffset\t\t{}",
            i,
            d.location,
            d.binding,
            to_string(d.format),
            d.offset
        ) << std::endl;
    }
}

void Print(const std::vector <vk::VertexInputBindingDescription> & desc) {
    for (size_t i = 0; i < desc.size(); i++) {
        const auto & d = desc[i];
        std::cout << std::format(
            "Description {}\n"
            "\tbinding\t\t{}\n"
            "\tstride\t\t{}\n"
            "\trate\t\t{}",
            i,
            d.binding,
            d.stride,
            to_string(d.inputRate)
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

    Print(attribute.ToVkVertexInputBinding());
    Print(attribute.ToVkVertexAttribute());

    std::cout << "Total size: " << attribute.GetTotalPerVertexSize() << std::endl ;
    auto offsets = attribute.EnumerateOffsetFactor();

    std::cout << "Offsets:";
    for (auto offset : offsets) {
        std::cout << " " << offset ;
    }
    std::cout << std::endl ;
}
