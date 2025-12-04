#ifndef PIPELINE_MATERIAL_MATERIALLIBRARY_INCLUDED
#define PIPELINE_MATERIAL_MATERIALLIBRARY_INCLUDED

#include <memory>

#include "Asset/InstantiatedFromAsset.h"
#include "Asset/Material/MaterialLibraryAsset.h"
#include "Render/Renderer/HomogeneousMesh.h"

namespace Engine {

    class RenderSystem;
    class MaterialTemplate;
    
    /**
     * @brief A bundle of materials organized by dispatcher.
     * 
     * `MaterialLibrary` acts as a bundle of different materials
     * (i.e. pipelines) that can be dispatched by tags and the type
     * of the mesh to be drawn.
     * 
     * This class acts as a dispatcher that dispatches draw calls
     * of renders to their corresponding pipeline based on the tag
     * and the type of the mesh.
     */
    class MaterialLibrary : public IInstantiatedFromAsset<MaterialLibraryAsset> {
        RenderSystem & m_system;
        struct impl;
        std::unique_ptr <impl> pimpl;

    public:
        MaterialLibrary(RenderSystem & system);
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
            VertexAttribute mesh_type
        ) const noexcept;

        MaterialTemplate * FindMaterialTemplate(
            const std::string & tag,
            VertexAttribute mesh_type
        ) noexcept;

        void PreheatMaterialTemplate(
            const std::string & tag,
            VertexAttribute mesh_type
        ) noexcept;

        void Instantiate (const MaterialLibraryAsset &) override;
    };
}

#endif // PIPELINE_MATERIAL_MATERIALLIBRARY_INCLUDED
