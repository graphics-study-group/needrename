#include "HomogeneousMesh.h"
#include <Asset/AssetRef.h>
#include <Asset/Mesh/MeshAsset.h>

#include "Render/Memory/DeviceBuffer.h"
#include "Render/Renderer/VertexAttribute.h"
#include "Render/RenderSystem/AllocatorState.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.hpp>

namespace Engine {

    struct HomogeneousMesh::impl {

        std::unique_ptr<DeviceBuffer> m_buffer{};
        std::vector<vk::DeviceSize> m_buffer_offsets{};

        bool m_updated{false};

        uint64_t m_total_allocated_buffer_size{0};

        std::shared_ptr<AssetRef> m_mesh_asset{};
        size_t m_submesh_idx{};
        VertexAttribute m_attribute{};

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
            return GetVertexIndexCount() * sizeof(uint32_t) + GetVertexCount() * m_attribute.GetTotalPerVertexSize();
        }
    };

    HomogeneousMesh::HomogeneousMesh(
        const RenderSystemState::AllocatorState &allocator,
        std::shared_ptr<AssetRef> mesh_asset,
        size_t submesh_idx
    ) : pimpl(std::make_unique<impl>()) {
        pimpl->m_mesh_asset = mesh_asset;
        pimpl->m_submesh_idx = submesh_idx;
        pimpl->m_buffer = nullptr;

        pimpl->FetchFromAsset(allocator);
    }

    HomogeneousMesh::~HomogeneousMesh() = default;

    /// @brief Fetch basic vertex info from asset. Actual data are not stored in this class.
    /// @param allocator 
    void HomogeneousMesh::impl::FetchFromAsset(const RenderSystemState::AllocatorState & allocator) {
        // Load vertex attribute format
        {
            const auto & asset = m_mesh_asset->as<MeshAsset>();
            const auto & submesh = asset->m_submeshes[m_submesh_idx];
            m_attribute = {};

            using enum MeshAsset::Submesh::Attributes::AttributeType;
            if (submesh.positions.type != Unused) {
                assert(submesh.positions.type == Floatx3);
                m_attribute.SetAttribute(VertexAttributeSemantic::Position, VertexAttributeType::SFloat32x3);
            }
            if (submesh.color.type != Unused) {
                assert(submesh.color.type == Floatx3);
                m_attribute.SetAttribute(VertexAttributeSemantic::Color, VertexAttributeType::SFloat32x3);
            }
            if (submesh.normal.type != Unused) {
                assert(submesh.normal.type == Floatx3);
                m_attribute.SetAttribute(VertexAttributeSemantic::Normal, VertexAttributeType::SFloat32x3);
            }
            if (submesh.texcoord0.type != Unused) {
                assert(submesh.texcoord0.type == Floatx2);
                m_attribute.SetAttribute(VertexAttributeSemantic::Texcoord0, VertexAttributeType::SFloat32x2);
            }
        }
        
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
            m_buffer = DeviceBuffer::CreateUnique(
                allocator,
                {BufferTypeBits::Vertex, BufferTypeBits::Index, BufferTypeBits::CopyTo},
                buffer_size,
                "Buffer - mesh vertices"
            );
            m_total_allocated_buffer_size = buffer_size;

            // Generate buffer offsets
            m_buffer_offsets.clear();
            auto vec = m_attribute.EnumerateOffsetFactor();
            for (auto f : vec) {
                m_buffer_offsets.push_back(f * new_vertex_count);
            }
            m_buffer_offsets.push_back(new_vertex_count * m_attribute.GetTotalPerVertexSize());
        }
    }

    std::unique_ptr <DeviceBuffer> HomogeneousMesh::CreateStagingBuffer(const RenderSystemState::AllocatorState & allocator) const {
        pimpl->FetchFromAsset(allocator);

        const uint64_t buffer_size = GetExpectedBufferSize();

        auto buffer = DeviceBuffer::CreateUnique(
            allocator,
            {BufferTypeBits::StagingToDevice},
            buffer_size,
            "Buffer - mesh staging"
        );

        std::byte *data = buffer->GetVMAddress();
        pimpl->WriteToMemory(data);
        buffer->Flush();

        return buffer;
    }

    void HomogeneousMesh::impl::WriteToMemory(std::byte *pointer) const {
        uint64_t offset = 0;
        auto &mesh_asset = *m_mesh_asset->as<MeshAsset>();
        auto &submesh = mesh_asset.m_submeshes[m_submesh_idx];
        const auto &indices = mesh_asset.m_submeshes[m_submesh_idx].m_indices;

        for (uint32_t i = 0; i < 16; i++) {
            auto semantic = static_cast<VertexAttributeSemantic>(i);
            if (!m_attribute.HasAttribute(semantic)) continue;

            switch(semantic) {
                using enum VertexAttributeSemantic;
                case Position:
                    // TODO: We assume that input read from mesh asset and data submitted to 
                    // GPU is the same. We might need to renormalize the input sometimes.
                    assert(submesh.positions.type == MeshAsset::Submesh::Attributes::AttributeType::Floatx3);
                    std::memcpy(
                        pointer + m_attribute.GetOffsetFactor(semantic) * GetVertexCount(),
                        submesh.positions.attribf.data(),
                        m_attribute.GetPerVertexSize(semantic) * GetVertexCount()
                    );
                    break;
                case Normal: {
                    assert(submesh.normal.type == MeshAsset::Submesh::Attributes::AttributeType::Floatx3);
                    // TODO: How to deinterleave the data should depends on vertex attributes
                    // Now we just assume that it is defaulted.
                    std::memcpy(
                        pointer + m_attribute.GetOffsetFactor(semantic) * GetVertexCount(),
                        submesh.normal.attribf.data(),
                        m_attribute.GetPerVertexSize(semantic) * GetVertexCount()
                    );
                    break;
                }
                case Color: {
                    assert(submesh.color.type == MeshAsset::Submesh::Attributes::AttributeType::Floatx3);
                    std::memcpy(
                        pointer + m_attribute.GetOffsetFactor(semantic) * GetVertexCount(),
                        submesh.color.attribf.data(),
                        m_attribute.GetPerVertexSize(semantic) * GetVertexCount()
                    );
                    break;
                }
                case Texcoord0: {
                    assert(submesh.texcoord0.type == MeshAsset::Submesh::Attributes::AttributeType::Floatx2);
                    std::memcpy(
                        pointer + m_attribute.GetOffsetFactor(semantic) * GetVertexCount(),
                        submesh.texcoord0.attribf.data(),
                        m_attribute.GetPerVertexSize(semantic) * GetVertexCount()
                    );
                    break;
                default:
                    assert(!"Unimplemented");
                }
            }
        }
        // Index
        std::memcpy(pointer + m_attribute.GetTotalPerVertexSize() * GetVertexCount(), indices.data(), indices.size() * sizeof(uint32_t));
        offset += indices.size() * sizeof(uint32_t);
    }

    HomogeneousMesh::BufferBindingInfo HomogeneousMesh::GetIndexBufferBinding() const noexcept {
        assert(pimpl->m_buffer->GetBuffer());
        // Last offset is the offset of index buffer.
        return {
            pimpl->m_buffer.get(),
            *(pimpl->m_buffer_offsets.rbegin()),
            0
        };
    }

    VertexAttribute HomogeneousMesh::GetVertexAttributeFormat() const noexcept {
        return pimpl->m_attribute;
    }

    uint32_t HomogeneousMesh::GetIndexCount() const noexcept {
        return pimpl->GetVertexIndexCount();
    }

    uint32_t HomogeneousMesh::GetVertexAttributeCount() const noexcept {
        return pimpl->GetVertexCount();
    }

    uint64_t HomogeneousMesh::GetExpectedBufferSize() const {
        return pimpl->GetExpectedBufferSize();
    }

    const DeviceBuffer &HomogeneousMesh::GetBuffer() const {
        return *pimpl->m_buffer;
    }

    void HomogeneousMesh::FillVertexAttributeBufferBindings(std::vector <HomogeneousMesh::BufferBindingInfo> & v) const noexcept {
        assert(pimpl->m_buffer->GetBuffer());
        v.clear();
        v.reserve(pimpl->m_buffer_offsets.size());
        std::transform(
            pimpl->m_buffer_offsets.begin(),
            pimpl->m_buffer_offsets.end() - 1,
            std::back_inserter(v),
            [this] (size_t offset) -> BufferBindingInfo {
                return {
                    pimpl->m_buffer.get(),
                    offset,
                    0
                };
            }
        );
    }
} // namespace Engine
