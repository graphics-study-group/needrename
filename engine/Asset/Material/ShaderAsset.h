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

        /// @brief How frequently the value this variable changes?
        /// Corresponds to descriptor set index of this variable.
        enum class Frequency {
            PerScene = 0,
            PerMaterial = 1,
            PerModel = 2
        } frequency;

        /// @brief what is the type of this variable?
        enum class Type {
            Float,
            Int,
            Vec4,
            Mat4,
            Texture
        } type;

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
        uint32_t binding;

        /// @brief Offset of this uniform variable in UBO.
        /// Ignored for non-UBO variables such as textures.
        uint32_t offset;
    };

    class REFL_SER_CLASS(REFL_WHITELIST) ShaderAsset : public Asset {
        REFL_SER_BODY(ShaderAsset)
    public:
        REFL_ENABLE ShaderAsset () = default;

        enum class ShaderType {
            None,
            Vertex,
            Fragment
        } shaderType {ShaderType::None};

        // XXX: We currently use filename only. Ideally we should directly store SPIR-V binaries.
        std::string filename {};
        // std::vector <uint32_t *> binary;
    };
}

#endif // ASSET_MATERIAL_SHADERASSET_INCLUDED
