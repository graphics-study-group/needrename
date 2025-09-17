#ifndef ASSET_MATERIAL_MATERIALASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALASSET_INCLUDED

#include <Asset/Asset.h>
#include <Asset/Material/ShaderAsset.h>
#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_unordered_map.h>
#include <any>
#include <glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace Engine {
    class ObjLoader;
    class AssetRef;

    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialProperty {
        REFL_SER_BODY(MaterialProperty)

        enum class InBlockVarType {
            Undefined,
            Float,
            Int,
            Vec4,
            Mat4
        };

        enum class Type {
            Undefined,
            // Uniform buffer object, i.e. a buffer for bulk uniform variables.
            UBO,
            // Shader storage buffer object.
            StorageBuffer,
            // Texture to be sampled (or combined image sampler).
            Texture,
            // Storage image. Generally used in compute shaders.
            StorageImage,
        } type{};

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

    class REFL_SER_CLASS(REFL_WHITELIST) MaterialAsset : public Asset {
        REFL_SER_BODY(MaterialAsset)
    public:
        REFL_ENABLE MaterialAsset() = default;
        virtual ~MaterialAsset() = default;

        REFL_SER_ENABLE std::string m_name{};
        REFL_SER_ENABLE std::shared_ptr<AssetRef> m_template{};
        REFL_SER_ENABLE std::unordered_map<std::string, MaterialProperty> m_properties{};

        friend class ObjLoader;
    };
} // namespace Engine

#endif // ASSET_MATERIAL_MATERIALASSET_INCLUDED
