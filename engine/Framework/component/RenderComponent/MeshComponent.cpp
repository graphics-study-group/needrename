#include "MeshComponent.h"
#include "tiny_obj_loader.h"
#include <iostream>
#include <cassert>

namespace Engine
{
    MeshComponent::MeshComponent(
        std::shared_ptr<Material> mat, 
        std::weak_ptr<GameObject> gameObject
    ) : RendererComponent(mat, gameObject)
    {
        m_VAO = 0;
        m_VBO[0] = m_VBO[1] = 0;
    }

    MeshComponent::~MeshComponent()
    {
        if (glIsVertexArray(m_VAO)) {
            glDeleteVertexArrays(1, &m_VAO);
            glDeleteBuffers(2, m_VBO);
        }
    }

    void MeshComponent::Tick(float dt)
    {
    }

    void MeshComponent::Draw()
    {
        GLenum glError;
        m_material->PrepareDraw(/*Context, Transform, etc.*/);

        glBindVertexArray(m_VAO);
        glDrawArrays(GL_TRIANGLES, 0, m_position.size() / 3);
        
        glError = glGetError();
        if(glError != GL_NO_ERROR) {
            throw std::runtime_error("Cannot draw VAO.");
        }
    }

    bool MeshComponent::ReadAndFlatten(const char *filename)
    {
        m_position.clear();
        m_uv.clear();

        tinyobj::ObjReader reader;
        reader.ParseFromFile(filename);

        if (!reader.Error().empty()) {
            std::cerr << "TinyObjReader reports errors reading " << filename << std::endl ;
            std::cerr << "\t" << reader.Error() << std::endl;
            return false;
        }

        if (!reader.Warning().empty()) {
            std::cerr << "TinyObjReader reports warnings reading " << filename << std::endl ;
            std::cerr << "\t" << reader.Warning() << std::endl;
        }

        const tinyobj::attrib_t & attrib = reader.GetAttrib();
        const std::vector<tinyobj::shape_t> & shapes = reader.GetShapes();

        m_position.reserve(shapes.size() * 3);
        m_uv.reserve(shapes.size() * 3);

        for (size_t s = 0; s < shapes.size(); s++) {
            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
                size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);
                assert(fv == 3 && "Mesh is not triangulated");
                for (size_t v = 0; v < fv; v++) {
                    tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];
                    float vx = attrib.vertices[3*size_t(idx.vertex_index)+0];
                    float vy = attrib.vertices[3*size_t(idx.vertex_index)+1];
                    float vz = attrib.vertices[3*size_t(idx.vertex_index)+2];

                    assert(idx.texcoord_index >= 0 && "Missing texture coordinate.");
                    float tx = attrib.texcoords[2*size_t(idx.texcoord_index)+0];
                    float ty = attrib.texcoords[2*size_t(idx.texcoord_index)+1];

                    // Flatten coordinates for VBO
                    m_position.push_back(vx);
                    m_position.push_back(vy);
                    m_position.push_back(vz);
                    m_uv.push_back(tx);
                    m_uv.push_back(ty);
                }
                index_offset += fv;
            }
        }

        m_position.shrink_to_fit();
        m_uv.shrink_to_fit();
        assert(m_position.size() % 3 == 0 && m_uv.size() % 2 == 0);
        assert(m_position.size() / 3 == m_uv.size() / 2);

        if (m_position.empty())
            return false;
        this->SetupVertices();
        return true;
    }

    void MeshComponent::SetupVertices()
    {
        if (glIsVertexArray(m_VAO)) {
            glDeleteVertexArrays(1, &m_VAO);
            glDeleteBuffers(2, m_VBO);

            m_VAO = 0;
            m_VBO[0] = m_VBO[1] = 0;
        }
        GLenum glError;

        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(2, m_VBO);
        glBindVertexArray(m_VAO);

        glError = glGetError();
        if(glError != GL_NO_ERROR) {
            throw std::runtime_error("Failed to allocate VAO and VBO");
        }
        
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO[0]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * m_position.size(), m_position.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindBuffer(GL_ARRAY_BUFFER, m_VBO[1]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * m_uv.size(), m_uv.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);

        glError = glGetError();
        if(glError != GL_NO_ERROR) {
            throw std::runtime_error("Failed to write VAO and VBO");
        }

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
    }
}
