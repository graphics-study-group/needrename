#ifndef PIPELINE_MATERIAL_MATERIALLIBRARY_INCLUDED
#define PIPELINE_MATERIAL_MATERIALLIBRARY_INCLUDED

#include <memory>

#include "Asset/InstantiatedFromAsset.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "Render/Renderer/HomogeneousMesh.h"

namespace Engine {

    class MaterialTemplate;

    class MaterialLibrary : public IInstantiatedFromAsset<MaterialLibraryAsset> {
        struct impl;
        std::unique_ptr <impl> pimpl;

    public:
        MaterialLibrary();
        ~MaterialLibrary();

        /**
         * @brief Dispatch a material template by tag and mesh type.
         * 
         * The pipeline is first by tags, and then by mesh type.
         * It is selected to guarantee that no mesh vertex attributes
         * are left out by the vertex shader input.
         */
        const MaterialTemplate * FindMaterialTemplate(
            const std::string & tag,
            HomogeneousMesh::MeshVertexType mesh_type
        ) const noexcept;

        void Instantiate (const MaterialLibraryAsset &) override;
    };
}

#endif // PIPELINE_MATERIAL_MATERIALLIBRARY_INCLUDED
