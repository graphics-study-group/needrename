#ifndef ASSET_MATERIAL_MATERIALASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALASSET_INCLUDED

#include <memory>
#include <string>
#include <any>
#include <glm.hpp>
#include <unordered_map>
#include <Asset/Asset.h>
#include <meta_engine/reflection.hpp>

namespace Engine
{
    class ObjLoader;
    class TextureAsset;

    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialProperty
    {
        REFL_SER_BODY(MaterialProperty)

        enum class Type
        {
            Float,
            Int,
            Vec4,
            Mat4,
            Texture
        };

        Type m_type{};
        std::any m_value{};

        void save_to_archive(Serialization::Archive &archive) const;
        void load_from_archive(Serialization::Archive &archive);

        MaterialProperty() = default;
        MaterialProperty(float value);
        MaterialProperty(int value);
        MaterialProperty(const glm::vec4 &value);
        MaterialProperty(const glm::mat4 &value);
        MaterialProperty(const std::shared_ptr<TextureAsset> &value);
        virtual ~MaterialProperty() = default;
    };

    class REFL_SER_CLASS(REFL_WHITELIST) MaterialAsset : public Asset
    {
        REFL_SER_BODY(MaterialAsset)
    public:
        REFL_ENABLE MaterialAsset() = default;
        virtual ~MaterialAsset() = default;

        REFL_SER_ENABLE std::string m_name{};
        // TODO: REFL_SER_ENABLE std::shared_ptr<ShaderAsset> m_shader{};
        REFL_SER_ENABLE std::unordered_map<std::string, MaterialProperty> m_properties{};

        friend class ObjLoader;
    };
}

#endif // ASSET_MATERIAL_MATERIALASSET_INCLUDED
