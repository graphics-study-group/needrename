#ifndef ASSET_MATERIAL_MTLMATERIALASSET_INCLUDED
#define ASSET_MATERIAL_MTLMATERIALASSET_INCLUDED

#include <memory>
#include <glm.hpp>
#include <Asset/Asset.h>
#include <Asset/Texture/TextureAsset.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class REFL_SER_CLASS(REFL_BLACKLIST) MtlMaterialAsset : public Asset
    {
    public:
        MtlMaterialAsset() = default;
        virtual ~MtlMaterialAsset() = default;

    public:
        std::string m_name;

        glm::vec3 m_ambient;
        glm::vec3 m_diffuse;
        glm::vec3 m_specular;
        glm::vec3 m_transmittance;
        glm::vec3 m_emission;
        float m_shininess;
        float m_ior;
        float m_dissolve;
        int m_illum;

        std::shared_ptr<TextureAsset> m_ambient_tex{};
        std::shared_ptr<TextureAsset> m_diffuse_tex{};
        std::shared_ptr<TextureAsset> m_specular_tex{};
        std::shared_ptr<TextureAsset> m_specular_highlight_tex{};
        std::shared_ptr<TextureAsset> m_bump_tex{};
        std::shared_ptr<TextureAsset> m_displacement_tex{};
        std::shared_ptr<TextureAsset> m_alpha_tex{};
        std::shared_ptr<TextureAsset> m_reflection_tex{};

        float m_roughness;
        float m_metallic;
        float m_sheen;
        float m_clearcoat_thickness;
        float m_clearcoat_roughness;
        float m_anisotropy;
        float m_anisotropy_rotation;

        std::shared_ptr<TextureAsset> m_roughness_tex{};
        std::shared_ptr<TextureAsset> m_metallic_tex{};
        std::shared_ptr<TextureAsset> m_sheen_tex{};
        std::shared_ptr<TextureAsset> m_emissive_tex{};
        std::shared_ptr<TextureAsset> m_normal_tex{};
    };
}

#endif // ASSET_MATERIAL_MTLMATERIALASSET_INCLUDED
