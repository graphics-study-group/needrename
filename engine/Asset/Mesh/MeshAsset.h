#ifndef ASSET_MESH_MESHASSET_INCLUDED
#define ASSET_MESH_MESHASSET_INCLUDED

#include <Asset/Asset.h>
#include <Reflection/macros.h>
#include <vector>
#include <variant>

namespace Engine {
    class ObjLoader;

    class REFL_SER_CLASS(REFL_WHITELIST) MeshAsset : public Asset {
        REFL_SER_BODY(MeshAsset)

        /**
         * @brief Determines the expected buffer size of a submesh.
         * This method is only used for serialization and deserialization.
         * To calculate the buffer size of a mesh, you should use the
         * `HomogeneousMesh` version of this method.
         */
        REFL_ENABLE uint64_t GetSubmeshExpectedBufferSize(size_t submesh_idx) const;

    public:
        REFL_ENABLE MeshAsset();
        virtual ~MeshAsset();

        friend class ObjLoader;

        struct Submesh {
            std::vector<uint32_t> m_indices{};

            struct Attributes {
                enum class AttributeType : uint32_t {
                    Unused,
                    Floatx1,
                    Floatx2,
                    Floatx3,
                    Floatx4,
                    Uintx1,
                    Uintx2,
                    Uintx3,
                    Uintx4
                } type {};

                std::vector <float> attribf{};
                std::vector <uint32_t> attribu{};

                static constexpr uint8_t GetStride(AttributeType type) {
                    switch(type) {
                        using enum AttributeType;
                        case Floatx1:
                        case Uintx1:
                            return 1;
                        case Floatx2:
                        case Uintx2:
                            return 2;
                        case Floatx3:
                        case Uintx3:
                            return 3;
                        case Floatx4:
                        case Uintx4:
                            return 4;
                        default:
                            return 0;
                    }
                }
            };

            uint32_t vertex_count {};
            Attributes positions {};
            Attributes color {}, normal {}, texcoord0 {};
            Attributes tangent {}, texcoord1 {}, texcoord2 {}, texcoord3 {};
            Attributes bone_indices {}, bone_weights {};
        };

        REFL_ENABLE size_t GetSubmeshCount() const;
        REFL_ENABLE uint32_t GetSubmeshVertexIndexCount(size_t submesh_idx) const;
        REFL_ENABLE uint32_t GetSubmeshVertexCount(size_t submesh_idx) const;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;

        std::string m_name{};
        std::vector<Submesh> m_submeshes{};
    };
} // namespace Engine

#endif // ASSET_MESH_MESHASSET_INCLUDED
