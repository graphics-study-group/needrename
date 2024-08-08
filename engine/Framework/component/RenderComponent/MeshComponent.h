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

        virtual void Tick(float dt) override;
        virtual void Draw(CameraContext context) override;

        bool ReadAndFlatten(std::filesystem::path path);

    protected:
        typedef std::vector <float> Positions;
        typedef std::vector <float> UVs;

        std::filesystem::path m_model_absolute_path{};

        std::vector <Positions> m_position{};
        std::vector <UVs> m_uv{};

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
