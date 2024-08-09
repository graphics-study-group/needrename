#ifndef FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
#define FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED

#include <memory>
#include <vector>
#include <filesystem>
#include <GLAD/glad.h>
#include "Core/Math/Transform.h"
#include "Render/Material/Material.h"
#include "Framework/component/RenderComponent/RendererComponent.h"
#include "Framework/go/GameObject.h"
#include "Asset/Mesh/Mesh.h"

namespace tinyobj
{
    class material_t;
}

namespace Engine
{
    class Component;
    class GameObject;

    class MeshComponent : public RendererComponent
    {
    public:
        MeshComponent(std::weak_ptr<GameObject> gameObject);
        virtual ~MeshComponent();

        virtual void Load() override;
        virtual void Unload() override;
        virtual void Tick(float dt) override;
        virtual void Draw(CameraContext context) override;

        virtual void SetMesh(std::shared_ptr<Mesh> mesh);
        virtual void AddMaterial(std::shared_ptr<Material> material);

    protected:
        std::shared_ptr<Mesh> m_mesh;

        std::vector <size_t> m_array_size;
        std::vector <GLuint> m_VAOs{};
        std::vector <GLuint> m_VBOs_position{};
        std::vector <GLuint> m_VBOs_uv{};

        /// @brief 
        /// @return True if successful
        bool SetObjMaterial(size_t id, const tinyobj::material_t & obj_material);

        void SetDefaultMaterial();

        /// @brief Setup VAO and VBOs for OpenGL rendering
        bool SetupVertices();
    };
}
#endif // FRAMEWORK_COMPONENT_RENDERCOMPONENT_MESHCOMPONENT_INCLUDED
