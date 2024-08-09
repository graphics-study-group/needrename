#include "MeshComponent.h"

#include "Render/NativeResource/ImmutableTexture2D.h"
#include "Render/Material/SingleColor.h"
#include "Render/Material/Shadeless.h"
#include "Framework/component/RenderComponent/CameraComponent.h"

#include <tiny_obj_loader.h>
#include <SDL3/SDL.h>

#include <iostream>
#include <cassert>
#include <random>

#include <codecvt>
#include <locale>

namespace Engine
{
    MeshComponent::MeshComponent(
        std::weak_ptr<GameObject> gameObject) : RendererComponent(gameObject)
    {
    }

    MeshComponent::~MeshComponent()
    {
    }

    void MeshComponent::Load()
    {
        m_mesh->Load();
        for (auto &material : m_materials)
        {
            material->Load();
        }
        SetupVertices();
    }

    void MeshComponent::Unload()
    {
        m_mesh->Unload();
        for (auto &material : m_materials)
        {
            material->Unload();
        }
    }

    void MeshComponent::Tick(float dt)
    {
    }

    void MeshComponent::Draw(CameraContext context)
    {
        GLenum glError;
        for (size_t i = 0; i < m_materials.size(); i++)
        {

            m_materials[i]->PrepareDraw(context, this->CreateContext());

            glBindVertexArray(m_VAOs[i]);
            glDrawArrays(GL_TRIANGLES, 0, m_array_size[i]);

            glError = glGetError();
            if (glError != GL_NO_ERROR)
            {
                throw std::runtime_error("Cannot draw VAO.");
            }
        }
    }

    void MeshComponent::SetMesh(std::shared_ptr<Mesh> mesh)
    {
        m_mesh = mesh;
    }

    void MeshComponent::AddMaterial(std::shared_ptr<Material> material)
    {
        m_materials.push_back(material);
    }

    bool MeshComponent::SetupVertices()
    {
        GLenum glError;

        size_t submesh_count = m_mesh->GetSubmeshCount();
        m_array_size.resize(submesh_count);
        m_VAOs.resize(submesh_count);
        m_VBOs_position.resize(submesh_count);
        m_VBOs_uv.resize(submesh_count);

        glGenVertexArrays(submesh_count, m_VAOs.data());
        glGenBuffers(submesh_count, m_VBOs_position.data());
        glGenBuffers(submesh_count, m_VBOs_uv.data());
        glError = glGetError();
        if (glError != GL_NO_ERROR)
        {
            SDL_LogError(0, "Failed to allocate VAO and VBO.");
            return false;
        }

        auto &mesh_offsets = m_mesh->GetOffsets();
        auto &mesh_triangles = m_mesh->GetTriangles();
        auto &mesh_position = m_mesh->GetPositions();
        auto &mesh_uv = m_mesh->GetUVs();

        std::vector<float> tmp_position;
        std::vector<float> tmp_uv;
        for (size_t i = 0; i < submesh_count; i++)
        {
            tmp_position.clear();
            tmp_uv.clear();
            size_t start = mesh_offsets[i];
            size_t end = i + 1 < mesh_offsets.size() ? mesh_offsets[i + 1] : mesh_triangles.size();
            for (size_t j = start; j < end; j++)
            {
                size_t index = mesh_triangles[j] * 3;
                tmp_position.push_back(mesh_position[index]);
                tmp_position.push_back(mesh_position[index + 1]);
                tmp_position.push_back(mesh_position[index + 2]);
                size_t uv_index = mesh_triangles[j] * 2;
                tmp_uv.push_back(mesh_uv[uv_index]);
                tmp_uv.push_back(mesh_uv[uv_index + 1]);
            }
            m_array_size[i] = tmp_position.size() / 3;

            glBindVertexArray(m_VAOs[i]);
            glError = glGetError();
            if (glError != GL_NO_ERROR)
            {
                SDL_LogError(0, "Failed to bind VAO and VBO for material %llu", i);
                return false;
            }

            glBindBuffer(GL_ARRAY_BUFFER, m_VBOs_position[i]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * tmp_position.size(), tmp_position.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
            glBindBuffer(GL_ARRAY_BUFFER, m_VBOs_uv[i]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * tmp_uv.size(), tmp_uv.data(), GL_STATIC_DRAW);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
            glError = glGetError();
            if (glError != GL_NO_ERROR)
            {
                SDL_LogError(0, "Failed to write VAO and VBO for material %llu", i);
                return false;
            }

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glError = glGetError();
            if (glError != GL_NO_ERROR)
            {
                SDL_LogError(0, "Failed to enable vertex attribute for material %llu", i);
                return false;
            }
        }
        return true;
    }
}
