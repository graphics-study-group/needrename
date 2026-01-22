#include "MaterialAsset.h"
#include <Asset/AssetRef.h>
#include <Reflection/serialization_glm.h>
#include <Reflection/serialization_smart_pointer.h>

namespace Engine {
    MaterialProperty::MaterialProperty(float value) :
        m_type(Type::Simple), m_ubo_type(InBlockVarType::Float), m_value(value) {
    }

    MaterialProperty::MaterialProperty(int value) :
        m_type(Type::Simple), m_ubo_type(InBlockVarType::Int), m_value(value) {
    }

    MaterialProperty::MaterialProperty(const glm::vec4 &value) :
        m_type(Type::Simple), m_ubo_type(InBlockVarType::Vec4), m_value(value) {
    }

    MaterialProperty::MaterialProperty(const glm::mat4 &value) :
        m_type(Type::Simple), m_ubo_type(InBlockVarType::Mat4), m_value(value) {
    }

    MaterialProperty::MaterialProperty(const std::shared_ptr<AssetRef> &value, Type type) :
        m_type(type), m_ubo_type(InBlockVarType::Undefined), m_value(value) {
        assert(type == Type::Texture || type == Type::CubeTexture);
    }

    void MaterialProperty::save_to_archive(Serialization::Archive &archive) const {
        Serialization::Json &json = *archive.m_cursor;
        switch (m_type) {
        case Type::UBO:
            // Ignore UBO
            break;
        case Type::SSBO:
            assert(!"Unimplemented");
            break;
        case Type::Texture: {
            json["m_type"] = "Texture";
            json["m_value"] = Serialization::Json::object();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::serialize(std::any_cast<std::shared_ptr<AssetRef>>(m_value), temp_archive);
            break;
        }
        case Type::CubeTexture: {
            json["m_type"] = "CubeTexture";
            json["m_value"] = Serialization::Json::object();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::serialize(std::any_cast<std::shared_ptr<AssetRef>>(m_value), temp_archive);
            break;
        }
        case Type::StorageImage: {
            json["m_type"] = "StorageImage";
            json["m_value"] = Serialization::Json::object();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::serialize(std::any_cast<std::shared_ptr<AssetRef>>(m_value), temp_archive);
            break;
        }
        case Type::Simple:
            json["m_type"] = "Simple";
            switch (m_ubo_type) {
            case InBlockVarType::Float: {
                json["m_ubo_type"] = "Float";
                json["m_value"] = std::any_cast<float>(m_value);
                break;
            }
            case InBlockVarType::Int: {
                json["m_ubo_type"] = "Int";
                json["m_value"] = std::any_cast<int>(m_value);
                break;
            }
            case InBlockVarType::Vec4: {
                json["m_ubo_type"] = "Vec4";
                json["m_value"] = Serialization::Json::object();
                Serialization::Archive temp_archive(archive, &json["m_value"]);
                Serialization::serialize(std::any_cast<glm::vec4>(m_value), temp_archive);
                break;
            }
            case InBlockVarType::Mat4: {
                json["m_ubo_type"] = "Mat4";
                json["m_value"] = Serialization::Json::object();
                Serialization::Archive temp_archive(archive, &json["m_value"]);
                Serialization::serialize(std::any_cast<glm::mat4>(m_value), temp_archive);
                break;
            }
            case InBlockVarType::Undefined:
            default:
                assert(!"Unidentified variable type.");
                break;
            }
            break;
        case Type::Undefined:
        default:
            assert(!"Unidentified property type.");
            break;
        }
    }

    void MaterialProperty::load_from_archive(Serialization::Archive &archive) {
        Serialization::Json &json = *archive.m_cursor;
        std::string type = json["m_type"];
        if (type == "Simple") {
            std::string ubo_type = json["m_ubo_type"];
            m_type = Type::Simple;
            if (ubo_type == "Float") {
                m_ubo_type = InBlockVarType::Float;
                m_value = json["m_value"].get<float>();
            } else if (ubo_type == "Int") {
                m_ubo_type = InBlockVarType::Int;
                m_value = json["m_value"].get<int>();
            } else if (ubo_type == "Mat4") {
                m_ubo_type = InBlockVarType::Mat4;
                m_value = glm::mat4{};
                Serialization::Archive temp_archive(archive, &json["m_value"]);
                Serialization::deserialize(std::any_cast<glm::mat4 &>(m_value), temp_archive);
            } else if (ubo_type == "Vec4") {
                m_ubo_type = InBlockVarType::Vec4;
                m_value = glm::vec4{};
                Serialization::Archive temp_archive(archive, &json["m_value"]);
                Serialization::deserialize(std::any_cast<glm::vec4 &>(m_value), temp_archive);
            }
        } else if (type == "Texture") {
            m_type = Type::Texture;
            m_value = std::make_shared<AssetRef>();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::deserialize(std::any_cast<std::shared_ptr<AssetRef> &>(m_value), temp_archive);
        } else if (type == "CubeTexture") {
            m_type = Type::CubeTexture;
            m_value = std::make_shared<AssetRef>();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::deserialize(std::any_cast<std::shared_ptr<AssetRef> &>(m_value), temp_archive);
        }
    }
} // namespace Engine
