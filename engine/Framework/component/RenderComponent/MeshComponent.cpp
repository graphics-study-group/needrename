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
        std::weak_ptr<GameObject> gameObject
    ) : RendererComponent(gameObject)
    {
    }

    MeshComponent::~MeshComponent()
    {
        glDeleteBuffers(m_VAOs.size(), m_VAOs.data());
        glDeleteBuffers(m_VBOs_position.size(), m_VBOs_position.data());
        glDeleteBuffers(m_VBOs_uv.size(), m_VBOs_uv.data());
    }

    void MeshComponent::Tick(float dt)
    {
    }

    void MeshComponent::Draw(CameraContext context)
    {
        GLenum glError;
        for (size_t i = 0; i < m_materials.size(); i++){

            m_materials[i]->PrepareDraw(context, this->CreateContext());

            glBindVertexArray(m_VAOs[i]);
            glDrawArrays(GL_TRIANGLES, 0, m_position[i].size() / 3);

            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                throw std::runtime_error("Cannot draw VAO.");
            }
        }
    }

    bool MeshComponent::ReadAndFlatten(std::filesystem::path path)
    {
        assert(m_materials.empty() && "Recreating meshes.");

        m_model_absolute_path = std::filesystem::absolute(path);
        
        tinyobj::ObjReader reader;
        reader.ParseFromFile(m_model_absolute_path.string());

        if (!reader.Error().empty()) {
            std::cerr << "TinyObjReader reports errors reading " << m_model_absolute_path << std::endl ;
            std::cerr << "\t" << reader.Error() << std::endl;
            return false;
        }

        if (!reader.Warning().empty()) {
            std::cerr << "TinyObjReader reports warnings reading " << m_model_absolute_path << std::endl ;
            std::cerr << "\t" << reader.Warning() << std::endl;
        }

        const tinyobj::attrib_t & attrib = reader.GetAttrib();
        const std::vector<tinyobj::shape_t> & shapes = reader.GetShapes();
        const std::vector<tinyobj::material_t> & materials = reader.GetMaterials();

        // Convert materials
        m_materials.resize(materials.size() + 1);
        SetDefaultMaterial();
        for (size_t i = 0; i < materials.size(); i++) {
            if(!SetObjMaterial(i+1, materials[i])) {
                m_materials.clear();
                return false;
            }
        }

        m_position.resize(m_materials.size());
        m_uv.resize(m_materials.size());

        // Create
        for (size_t s = 0; s < shapes.size(); s++) {
            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
                size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);
                assert(fv == 3 && "Mesh is not triangulated");

                size_t material_id = shapes[s].mesh.material_ids[f] + 1;
                assert(material_id <= m_materials.size()
                    && "Material ID out of range, check material conversion.");

                for (size_t v = 0; v < fv; v++) {
                    tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                    float vx = attrib.vertices[3*size_t(idx.vertex_index)+0];
                    float vy = attrib.vertices[3*size_t(idx.vertex_index)+1];
                    float vz = attrib.vertices[3*size_t(idx.vertex_index)+2];

                    assert(idx.texcoord_index >= 0 && "Missing texture coordinate.");
                    float tx = attrib.texcoords[2*size_t(idx.texcoord_index)+0];
                    float ty = attrib.texcoords[2*size_t(idx.texcoord_index)+1];

                    // Flatten coordinates for VBO
                    m_position[material_id].push_back(vx);
                    m_position[material_id].push_back(vy);
                    m_position[material_id].push_back(vz);
                    m_uv[material_id].push_back(tx);
                    m_uv[material_id].push_back(ty);
                }
                index_offset += fv;
            }
        }

        if (m_position.empty())
            return false;

        if (!this->SetupVertices())
            return false;
        return true;
    }

    bool MeshComponent::SetObjMaterial(size_t id, const tinyobj::material_t &obj_material)
    {
        auto material = std::make_shared<ShadelessMaterial>();
        auto texture = std::make_shared<ImmutableTexture2D>();
        // XXX: We need better asset managing system
        auto image_path = m_model_absolute_path.parent_path() / obj_material.diffuse_texname;
        if(!texture->LoadFromFile(image_path, GL_RGBA8, 1)) {
            return false;
        }
        material->SetAlbedo(texture);
        m_materials[id] = material;
        return true;
    }

    void MeshComponent::SetDefaultMaterial()
    {
        m_materials[0] = std::make_shared<SingleColor>(1.0, 0.0, 1.0, 1.0);
    }

    bool MeshComponent::SetupVertices()
    {
        GLenum glError;

        size_t material_count = this->m_materials.size();
        m_VAOs.resize(material_count);
        m_VBOs_position.resize(material_count);
        m_VBOs_uv.resize(material_count);

        glGenVertexArrays(material_count, m_VAOs.data());
        glGenBuffers(material_count, m_VBOs_position.data());
        glGenBuffers(material_count, m_VBOs_uv.data());
        glError = glGetError();
        if(glError != GL_NO_ERROR) {
            SDL_LogError(0, "Failed to allocate VAO and VBO.");
            return false;
        }

        for (size_t i = 0; i < material_count; i++) {
            assert(m_position[i].size() % 3 == 0);
            assert(m_uv[i].size() % 2 == 0);
            assert(m_position[i].size() / 3 == m_uv[i].size() / 2);

            glBindVertexArray(m_VAOs[i]);
            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                SDL_LogError(0, "Failed to bind VAO and VBO for material %llu", i);
                return false;
            }
            
            glBindBuffer(GL_ARRAY_BUFFER, m_VBOs_position[i]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * m_position[i].size(), m_position[i].data(), GL_STATIC_DRAW);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            glBindBuffer(GL_ARRAY_BUFFER, m_VBOs_uv[i]);
            glBufferData(GL_ARRAY_BUFFER, sizeof(float) * m_uv[i].size(), m_uv[i].data(), GL_STATIC_DRAW);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                SDL_LogError(0, "Failed to write VAO and VBO for material %llu", i);
                return false;
            }

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glError = glGetError();
            if(glError != GL_NO_ERROR) {
                SDL_LogError(0, "Failed to enable vertex attribute for material %llu", i);
                return false;
            }
        }
        return true;
    }
}
