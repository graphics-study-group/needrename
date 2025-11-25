#ifndef ASSET_MESH_PLANEMESHASSET_INCLUDED
#define ASSET_MESH_PLANEMESHASSET_INCLUDED

#include "Asset/Mesh/MeshAsset.h"
#include "Reflection/macros.h"
#include <cassert>

namespace Engine {
    /**
     * @brief A mesh asset containing a XY-plane facing Z-positive for testing.
     * 
     * This asset should not be serialized or deserialized.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) PlaneMeshAsset : public MeshAsset {
        REFL_SER_BODY(PlaneMeshAsset)
    public:
        PlaneMeshAsset() : MeshAsset() {
            this->m_submeshes.resize(1);
            this->m_submeshes[0] = {
                .m_indices = {0, 3, 2, 0, 2, 1},
                .m_positions =
                    {
                        {1.0f, -1.0f, 0.0f},
                        {1.0f, 1.0f, 0.0f},
                        {-1.0f, 1.0f, 0.0f},
                        {-1.0f, -1.0f, 0.0f},
                    },
            
                .m_attributes_basic = {
                    {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .texcoord1 = {1.0f, 0.0f}},
                    {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .texcoord1 = {1.0f, 1.0f}},
                    {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .texcoord1 = {0.0f, 1.0f}},
                    {.color = {1.0f, 1.0f, 1.0f}, .normal = {0.0f, 0.0f, 1.0f}, .texcoord1 = {0.0f, 0.0f}}
                },
            };
        }

        virtual void save_asset_to_archive(Serialization::Archive &) const override {
            assert(!"Serializing a run-time only test asset.");
        }
        virtual void load_asset_from_archive(Serialization::Archive &) override {
            assert(!"De-serializing a run-time only test asset.");
        }
    };
}

#endif // ASSET_MESH_PLANEMESHASSET_INCLUDED
