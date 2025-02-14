#ifndef ASSET_MATERIAL_SHADERASSET_INCLUDED
#define ASSET_MATERIAL_SHADERASSET_INCLUDED

#include <Asset/Asset.h>
#include <Asset/AssetRef.h>
#include <meta_engine/reflection.hpp>

namespace Engine {

    /// @brief Stores information of each uniform varible in a given shader file.
    /// Ideally it should be provided by a SPIR-V reflection library.
    /// We manually store them in MaterialTemplateAsset for now.
    struct REFL_SER_CLASS(REFL_WHITELIST) ShaderVariableProperty
    {
        REFL_SER_BODY(ShaderVariableProperty)

        REFL_ENABLE ShaderVariableProperty() = default;
        virtual ~ShaderVariableProperty() = default;

        /// @brief How frequently the value this variable changes?
        /// Corresponds to descriptor set index of this variable.
        REFL_SER_ENABLE enum class Frequency {
            PerScene = 0,
            PerCamera = 1,
            PerMaterial = 2,
            PerModel = 3
        } frequency {};

        /// @brief what is the type of this variable?
        REFL_SER_ENABLE enum class Type {
            Float,
            Int,
            Vec4,
            Mat4,
            Texture
        } type {};

        static constexpr size_t SizeOf(Type type) {
            switch(type) {
            case Type::Float:
            case Type::Int:
                return 4;
            case Type::Vec4:
                return 16;
            case Type::Mat4:
                return 64;
            default:
                return 0;
            }
        }

        static constexpr bool InUBO (Type type) {
            switch(type) {
            case Type::Float:
            case Type::Int:
            case Type::Vec4:
            case Type::Mat4:
                return true;
            default:
                return false;
            }
        }

        /// @brief Binding of this uniform, should be 0 for uniform variables in UBOs.
        REFL_SER_ENABLE uint32_t binding {};

        /// @brief Offset of this uniform variable in UBO.
        /// Ignored for non-UBO variables such as textures.
        REFL_SER_ENABLE uint32_t offset {};

        /// @brief Name of this uniform, which can be used as an alternative of binding or offset
        REFL_SER_ENABLE std::string name {};
    };

    class REFL_SER_CLASS(REFL_WHITELIST) ShaderAsset : public Asset {
        REFL_SER_BODY(ShaderAsset)
    public:
        REFL_ENABLE ShaderAsset () = default;

        REFL_SER_ENABLE enum class ShaderType {
            None,
            Vertex,
            Fragment
        } shaderType {ShaderType::None};

        REFL_SER_ENABLE std::string m_name {};
        std::vector <uint32_t> binary;

        virtual void save_asset_to_archive(Serialization::Archive &archive) const override;
        virtual void load_asset_from_archive(Serialization::Archive &archive) override;
    };
}

#endif // ASSET_MATERIAL_SHADERASSET_INCLUDED
