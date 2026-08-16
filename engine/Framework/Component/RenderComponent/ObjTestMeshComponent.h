#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED

#include "Asset/AssetManager/AssetManager.h"
#include "Framework/Component/RenderComponent/StaticMeshComponent.h"
#include "Framework/Import/ObjLoader.h"
#include "Framework/MainClass.h"
#include "Framework/framework_export.h"

namespace Engine {
    /**
     * @brief A mesh component that loads an obj file on construct.
     *
     * @warning For test only! Never use this component in production.
     * Always include this header after all your inclusion to avoid definition problems.
     */
    class ObjTestMeshComponent : public StaticMeshComponent {
    public:
        void LoadMesh(std::filesystem::path mesh) {
            ObjLoader loader{};
            ImportResult imported = loader.LoadObjInMemory(mesh);
            if (!imported.mesh_asset.IsValid()) {
                throw std::runtime_error("ObjLoader did not return a valid mesh asset.");
            }
            m_mesh_asset = imported.mesh_asset;
            m_material_assets = imported.mesh_material_assets;
        }

    public:
        ObjTestMeshComponent(const GameObject &parent) : StaticMeshComponent(parent) {
        }

        ~ObjTestMeshComponent() = default;
    };
} // namespace Engine

#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_OBJTESTMESHCOMPONENT_INCLUDED
