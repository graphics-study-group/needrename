#ifndef ASSET_MATERIAL_MATERIALASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALASSET_INCLUDED

#include <memory>
#include <string>
#include <any>
#include <glm.hpp>
#include <unordered_map>
#include <Asset/Asset.h>
#include <Asset/Material/ShaderAsset.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_unordered_map.h>

namespace Engine
{
    class ObjLoader;
    class AssetRef;

    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialProperty
    {
        REFL_SER_BODY(MaterialProperty)

        using Type = ShaderVariableProperty::Type;
        using InBlockVarType = ShaderInBlockVariableProperty::InBlockVarType;

        Type m_type{};
        InBlockVarType m_ubo_type{};
        std::any m_value{};

        void save_to_archive(Serialization::Archive &archive) const;
        void load_from_archive(Serialization::Archive &archive);

        MaterialProperty() = default;
        MaterialProperty(float value);
        MaterialProperty(int value);
        MaterialProperty(const glm::vec4 &value);
        MaterialProperty(const glm::mat4 &value);
        MaterialProperty(const std::shared_ptr<AssetRef> &value);
        virtual ~MaterialProperty() = default;
    };

    class REFL_SER_CLASS(REFL_WHITELIST) MaterialAsset : public Asset
    {
        REFL_SER_BODY(MaterialAsset)
    public:
        REFL_ENABLE MaterialAsset() = default;
        virtual ~MaterialAsset() = default;

        REFL_SER_ENABLE std::string m_name{};
        REFL_SER_ENABLE std::shared_ptr<AssetRef> m_template{};
        REFL_SER_ENABLE std::unordered_map<std::string, MaterialProperty> m_properties{};

        friend class ObjLoader;
    };
}

#endif // ASSET_MATERIAL_MATERIALASSET_INCLUDED
