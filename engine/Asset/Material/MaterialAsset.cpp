#include "MaterialAsset.h"
#include <Asset/Texture/Image2DTextureAsset.h>

namespace Engine
{
    void MaterialAsset::LoadFromTinyObj(const tinyobj::material_t &material, const std::filesystem::path &base_path)
    {
        m_name = material.name;
        m_vec4s["ambient"] = glm::vec4{material.ambient[0], material.ambient[1], material.ambient[2], 1.0f};
        m_vec4s["diffuse"] = glm::vec4{material.diffuse[0], material.diffuse[1], material.diffuse[2], 1.0f};
        m_vec4s["specular"] = glm::vec4{material.specular[0], material.specular[1], material.specular[2], 1.0f};
        m_vec4s["transmittance"] = glm::vec4{material.transmittance[0], material.transmittance[1], material.transmittance[2], 1.0f};
        m_vec4s["emission"] = glm::vec4{material.emission[0], material.emission[1], material.emission[2], 1.0f};
        m_floats["shininess"] = material.shininess;
        m_floats["ior"] = material.ior;
        m_floats["dissolve"] = material.dissolve;
        m_ints["illum"] = material.illum;
        if (!material.ambient_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.ambient_texname);
            m_textures["ambient"] = texture;
        }
        if (!material.diffuse_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.diffuse_texname);
            m_textures["diffuse"] = texture;
        }
        if (!material.specular_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.specular_texname);
            m_textures["specular"] = texture;
        }
        if (!material.specular_highlight_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.specular_highlight_texname);
            m_textures["specular_highlight"] = texture;
        }
        if (!material.bump_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.bump_texname);
            m_textures["bump"] = texture;
        }
        if (!material.displacement_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.displacement_texname);
            m_textures["displacement"] = texture;
        }
        if (!material.alpha_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.alpha_texname);
            m_textures["alpha"] = texture;
        }
        if (!material.roughness_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.roughness_texname);
            m_textures["roughness"] = texture;
        }
        if (!material.metallic_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.metallic_texname);
            m_textures["metallic"] = texture;
        }
        if (!material.sheen_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.sheen_texname);
            m_textures["sheen"] = texture;
        }
        if (!material.emissive_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.emissive_texname);
            m_textures["emissive"] = texture;
        }
        if (!material.normal_texname.empty())
        {
            auto texture = std::make_shared<Image2DTextureAsset>();
            texture->LoadFromFile(base_path / material.normal_texname);
            m_textures["normal"] = texture;
        }
        Asset::SetValid(true);
    }
}
