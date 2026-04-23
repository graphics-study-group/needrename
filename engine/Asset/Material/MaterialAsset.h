#ifndef ASSET_MATERIAL_MATERIALASSET_INCLUDED
#define ASSET_MATERIAL_MATERIALASSET_INCLUDED

#include <Asset/Asset.h>
#include <Asset/AssetRef.h>
#include <Asset/Shader/ShaderAsset.h>
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

    /**
     * @brief A single property of the material.
     * Can be a variable (e.g. vec4) or a reference to an object (e.g. texture)
     */
    struct REFL_SER_CLASS(REFL_WHITELIST) MaterialProperty {
        REFL_SER_BODY(MaterialProperty)

        /// @brief Type of the variable
        enum class InBlockVarType {
            Undefined,
            Float,
            Int,
            Vec4,
            Mat4
        };

        /// @brief Type of the property
        enum class Type {
            Undefined,
            // Uniform buffer object, i.e. a buffer for bulk uniform variables.
            // This type of variable is automatically managed by the material system
            // and should not appear in assets.
            UBO,
            // Shader storage buffer object.
            SSBO,
            // Texture to be sampled (or combined image sampler).
            Texture,
            // Cube texture to be sampled.
            CubeTexture,
            // Storage image. Generally used in compute shaders.
            StorageImage,
            // Simple variable that does not occupy a descriptor slot.
            Simple,
        } type{};

        Type m_type{};
        InBlockVarType m_ubo_type{};
        std::any m_value{};

        void save_to_archive(Serialization::Archive &archive) const;
        void load_from_archive(Serialization::Archive &archive);

        MaterialProperty() = default;
        /// @brief Create the property from a variable
        MaterialProperty(float value);
        MaterialProperty(int value);
        MaterialProperty(const glm::vec4 &value);
        MaterialProperty(const glm::mat4 &value);
        /// @brief Create the property from a reference to an object
        MaterialProperty(const AssetRef &value, Type type);
        virtual ~MaterialProperty() = default;
    };

    /**
     * @brief Asset for a material.
     * Contains a mapping from name to properties.
     */
    class REFL_SER_CLASS(REFL_WHITELIST) MaterialAsset : public Asset {
        REFL_SER_BODY(MaterialAsset)
    public:
        REFL_ENABLE MaterialAsset() = default;
        virtual ~MaterialAsset() = default;
        /// @brief Name of the material
        REFL_SER_ENABLE std::string m_name{};
        /// @brief Library of the material
        REFL_SER_ENABLE AssetRef m_library{};
        /// @brief Name to property mapping
        REFL_SER_ENABLE std::unordered_map<std::string, MaterialProperty> m_properties{};

        friend class ObjLoader;
    };
} // namespace Engine

#endif // ASSET_MATERIAL_MATERIALASSET_INCLUDED
