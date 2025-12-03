#include "HomogeneousMesh.h"
#include <Asset/AssetRef.h>
#include <Asset/Mesh/MeshAsset.h>

#include "Render/Memory/Buffer.h"
#include "Render/Renderer/VertexStruct.h"
#include "Render/RenderSystem/AllocatorState.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

namespace Engine {

    struct HomogeneousMesh::impl {

        static constexpr std::array<vk::VertexInputBindingDescription, 1> BINDINGS_POSITION = {
            vk::VertexInputBindingDescription{0, sizeof(VertexStruct::VertexPosition), vk::VertexInputRate::eVertex}
        };

        static constexpr std::array<vk::VertexInputAttributeDescription, 1> ATTRIBUTES_POSITON = {
            // Position
            vk::VertexInputAttributeDescription{0, BINDINGS_POSITION[0].binding, vk::Format::eR32G32B32Sfloat, 0},
        };

        static constexpr std::array<vk::VertexInputBindingDescription, 2> BINDINGS_BASIC = {
            // Position
            vk::VertexInputBindingDescription{0, sizeof(VertexStruct::VertexPosition), vk::VertexInputRate::eVertex},
            // Other attributes
            vk::VertexInputBindingDescription{1, sizeof(VertexStruct::VertexAttributeBasic), vk::VertexInputRate::eVertex}
        };
        static constexpr std::array<vk::VertexInputAttributeDescription, VertexStruct::VERTEX_ATTRIBUTE_BASIC_COUNT + 1>
            ATTRIBUTES_BASIC = {
                // Position
                vk::VertexInputAttributeDescription{0, BINDINGS_BASIC[0].binding, vk::Format::eR32G32B32Sfloat, 0},
                // Vertex color
                vk::VertexInputAttributeDescription{
                    1, BINDINGS_BASIC[1].binding, vk::Format::eR32G32B32Sfloat, VertexStruct::OFFSET_COLOR
                },
                // Vertex normal
                vk::VertexInputAttributeDescription{
                    2, BINDINGS_BASIC[1].binding, vk::Format::eR32G32B32Sfloat, VertexStruct::OFFSET_NORMAL
                },
                // Texcoord 1
                vk::VertexInputAttributeDescription{
                    3, BINDINGS_BASIC[1].binding, vk::Format::eR32G32Sfloat, VertexStruct::OFFSET_TEXCOORD1
                }
        };

        std::unique_ptr<Buffer> m_buffer{};
        std::vector<vk::DeviceSize> m_buffer_offsets{};

        bool m_updated{false};

        uint64_t m_total_allocated_buffer_size{0};

        std::shared_ptr<AssetRef> m_mesh_asset{};
        size_t m_submesh_idx{};
        MeshVertexType m_type{};

        void WriteToMemory(std::byte *pointer) const;
        /**
         * @brief Allocate buffer and update pre-calculated offsets.
         * Called before
         * `CreateStagingBuffer()`.
         */
        void FetchFromAsset(const RenderSystemState::AllocatorState & allocator);

        uint32_t GetVertexIndexCount() const {
            return m_mesh_asset->as<MeshAsset>()->GetSubmeshVertexIndexCount(m_submesh_idx);
        }

        uint32_t GetVertexCount() const {
            return m_mesh_asset->as<MeshAsset>()->GetSubmeshVertexCount(m_submesh_idx);
        }

        uint64_t GetExpectedBufferSize() const {
            auto vertex_cnt = GetVertexCount();
            uint64_t size = GetVertexIndexCount() * sizeof(uint32_t);
            switch (m_type) {
            case MeshVertexType::Skinned:
                size += vertex_cnt * sizeof(VertexStruct::VertexAttributeSkinned);
                [[fallthrough]];
            case MeshVertexType::Extended:
                size += vertex_cnt * sizeof(VertexStruct::VertexAttributeExtended);
                [[fallthrough]];
            case MeshVertexType::Basic:
                size += vertex_cnt * sizeof(VertexStruct::VertexAttributeBasic);
                [[fallthrough]];
            case MeshVertexType::Position:
                size += vertex_cnt * sizeof(VertexStruct::VertexPosition);
            }
            return size;
        }
    };

    HomogeneousMesh::HomogeneousMesh(
        const RenderSystemState::AllocatorState & allocator,
        std::shared_ptr<AssetRef> mesh_asset,
        size_t submesh_idx,
        MeshVertexType type
    ) : pimpl(std::make_unique<impl>()) {
        pimpl->m_mesh_asset = mesh_asset;
        pimpl->m_submesh_idx = submesh_idx;

        assert(type == MeshVertexType::Basic && "Unimplemented");
        pimpl->m_type = type;
        pimpl->m_buffer = nullptr;

        pimpl->FetchFromAsset(allocator);
    }

    HomogeneousMesh::~HomogeneousMesh() {
    }

    void HomogeneousMesh::impl::FetchFromAsset(const RenderSystemState::AllocatorState & allocator) {
        const uint64_t buffer_size = GetExpectedBufferSize();

        if (m_total_allocated_buffer_size != buffer_size) {
            m_total_allocated_buffer_size = 0;
            m_updated = true;

            const uint32_t new_vertex_count = GetVertexCount();
            const uint32_t new_vertex_index_count = GetVertexIndexCount();
            SDL_LogVerbose(
                SDL_LOG_CATEGORY_RENDER,
                "(Re-)Allocating buffer and memory for %u vertices and %u indices (%llu bytes).",
                new_vertex_count,
                new_vertex_index_count,
                buffer_size
            );
            m_buffer = Buffer::CreateUnique(allocator, Buffer::BufferType::Vertex, buffer_size, "Buffer - mesh vertices");
            m_total_allocated_buffer_size = buffer_size;

            // Generate buffer offsets
            m_buffer_offsets.clear();
            m_buffer_offsets.push_back(0);
            switch (m_type) {
            case MeshVertexType::Skinned:
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexPosition));
                m_buffer_offsets.push_back(
                    new_vertex_count * sizeof(VertexStruct::VertexAttributeBasic) + *m_buffer_offsets.rbegin()
                );
                m_buffer_offsets.push_back(
                    new_vertex_count * sizeof(VertexStruct::VertexAttributeExtended) + *m_buffer_offsets.rbegin()
                );
                m_buffer_offsets.push_back(
                    new_vertex_count * sizeof(VertexStruct::VertexAttributeSkinned) + *m_buffer_offsets.rbegin()
                );
                break;
            case MeshVertexType::Extended:
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexPosition));
                m_buffer_offsets.push_back(
                    new_vertex_count * sizeof(VertexStruct::VertexAttributeBasic) + *m_buffer_offsets.rbegin()
                );
                m_buffer_offsets.push_back(
                    new_vertex_count * sizeof(VertexStruct::VertexAttributeExtended) + *m_buffer_offsets.rbegin()
                );
                break;
            case MeshVertexType::Basic:
                m_buffer_offsets.push_back(new_vertex_count * sizeof(VertexStruct::VertexPosition));
                m_buffer_offsets.push_back(
                    new_vertex_count * sizeof(VertexStruct::VertexAttributeBasic) + *m_buffer_offsets.rbegin()
                );
            case MeshVertexType::Position:
            default:
                ;
            }

            assert(*m_buffer_offsets.rbegin() == buffer_size - new_vertex_index_count * sizeof(uint32_t));
        }
    }

    Buffer HomogeneousMesh::CreateStagingBuffer(const RenderSystemState::AllocatorState & allocator) const {
        pimpl->FetchFromAsset(allocator);

        const uint64_t buffer_size = GetExpectedBufferSize();

        Buffer buffer = Buffer::Create(allocator, Buffer::BufferType::Staging, buffer_size, "Buffer - mesh staging");

        std::byte *data = buffer.GetVMAddress();
        pimpl->WriteToMemory(data);
        buffer.Flush();

        return buffer;
    }

    void HomogeneousMesh::impl::WriteToMemory(std::byte *pointer) const {
        uint64_t offset = 0;
        auto &mesh_asset = *m_mesh_asset->as<MeshAsset>();
        const auto &positions = mesh_asset.m_submeshes[m_submesh_idx].m_positions;
        const auto &attributes = mesh_asset.m_submeshes[m_submesh_idx].m_attributes_basic;
        const auto &indices = mesh_asset.m_submeshes[m_submesh_idx].m_indices;
        // Position
        std::memcpy(&pointer[offset], positions.data(), positions.size() * sizeof(VertexStruct::VertexPosition));
        offset += positions.size() * sizeof(VertexStruct::VertexPosition);
        // Attributes
        std::memcpy(
            &pointer[offset], attributes.data(), attributes.size() * sizeof(VertexStruct::VertexAttributeBasic)
        );
        offset += attributes.size() * sizeof(VertexStruct::VertexAttributeBasic);
        // Index
        std::memcpy(&pointer[offset], indices.data(), indices.size() * sizeof(uint32_t));
        offset += indices.size() * sizeof(uint32_t);
    }

    vk::PipelineVertexInputStateCreateInfo HomogeneousMesh::GetVertexInputState(MeshVertexType type) {
        switch(type) {
        using enum MeshVertexType;
        case Position:
            return vk::PipelineVertexInputStateCreateInfo{
                vk::PipelineVertexInputStateCreateFlags{}, impl::BINDINGS_POSITION, impl::ATTRIBUTES_POSITON
            };
        case Basic:
            return vk::PipelineVertexInputStateCreateInfo{
                vk::PipelineVertexInputStateCreateFlags{}, impl::BINDINGS_BASIC, impl::ATTRIBUTES_BASIC
            };
        default:
            assert(!"Unimplemented");
        }
        
    }

    std::pair<vk::Buffer, uint64_t> HomogeneousMesh::GetIndexBufferInfo() const {
        assert(pimpl->m_buffer->GetBuffer());
        // Last offset is the offset of index buffer.
        return std::make_pair(pimpl->m_buffer->GetBuffer(), *(pimpl->m_buffer_offsets.rbegin()));
    }

    uint32_t HomogeneousMesh::GetVertexIndexCount() const {
        return pimpl->GetVertexIndexCount();
    }

    uint32_t HomogeneousMesh::GetVertexCount() const {
        return pimpl->GetVertexCount();
    }

    uint64_t HomogeneousMesh::GetExpectedBufferSize() const {
        return pimpl->GetExpectedBufferSize();
    }

    const Buffer &HomogeneousMesh::GetBuffer() const {
        return *pimpl->m_buffer;
    }

    std::pair<vk::Buffer, std::vector<uint64_t>> HomogeneousMesh::GetVertexBufferInfo() const {
        assert(pimpl->m_buffer->GetBuffer());
        return std::make_pair(pimpl->m_buffer->GetBuffer(), pimpl->m_buffer_offsets);
    }
} // namespace Engine
