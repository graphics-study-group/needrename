#ifndef RENDER_RENDERER_VERTEXSTRUCT_INCLUDED
#define RENDER_RENDERER_VERTEXSTRUCT_INCLUDED

#include <cctype>

namespace Engine {
    namespace VertexStruct {
        struct VertexPosition {
            float position[3];
        };

        struct VertexAttribute {
            float color[3];
            float normal[3];
            float texcoord1[2];
        };

        constexpr uint32_t OFFSET_COLOR = offsetof(VertexAttribute, color);
        constexpr uint32_t OFFSET_NORMAL = offsetof(VertexAttribute, normal);
        constexpr uint32_t OFFSET_TEXCOORD1 = offsetof(VertexAttribute, texcoord1);

        constexpr uint32_t VERTEX_ATTRIBUTE_COUNT = 3;
        constexpr size_t VERTEX_POSITION_SIZE = sizeof(VertexPosition);
        constexpr size_t VERTEX_ATTRIBUTE_SIZE = sizeof(VertexAttribute);
        constexpr size_t VERTEX_TOTAL_SIZE = VERTEX_POSITION_SIZE + VERTEX_ATTRIBUTE_SIZE;

        struct SkinnedVertexAttribute {
            uint32_t bone_index[4];
            float weight[4];
        };

        constexpr uint32_t OFFSET_BONE_INDEX = offsetof(SkinnedVertexAttribute, bone_index);
        constexpr uint32_t OFFSET_WEIGHT = offsetof(SkinnedVertexAttribute, weight);

        constexpr uint32_t SKINNED_VERTEX_ATTRIBUTE_COUNT = 2;
        constexpr size_t SKINNED_VERTEX_ATTRIBUTE_SIZE = sizeof(SkinnedVertexAttribute);
        constexpr size_t SKINNED_VERTEX_TOTAL_SIZE = VERTEX_TOTAL_SIZE + SKINNED_VERTEX_ATTRIBUTE_SIZE;
    };
};

#endif // RENDER_RENDERER_VERTEXSTRUCT_INCLUDED
