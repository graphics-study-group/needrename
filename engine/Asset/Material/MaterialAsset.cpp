#include "MaterialAsset.h"
#include <Asset/AssetRef.h>
#include <Reflection/serialization_glm.h>
#include <Reflection/serialization_smart_pointer.h>

namespace Engine
{
    MaterialProperty::MaterialProperty(float value)
        : m_type(Type::Undefined), m_ubo_type(UBOType::Float), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(int value)
        : m_type(Type::Undefined), m_ubo_type(UBOType::Int), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(const glm::vec4 &value)
        : m_type(Type::Undefined), m_ubo_type(UBOType::Vec4), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(const glm::mat4 &value)
        : m_type(Type::Undefined), m_ubo_type(UBOType::Mat4), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(const std::shared_ptr<AssetRef> &value)
        : m_type(Type::Texture), m_ubo_type(UBOType::Undefined), m_value(value)
    {
    }

    void MaterialProperty::save_to_archive(Serialization::Archive &archive) const
    {
        Serialization::Json &json = *archive.m_cursor;
        switch (m_type)
        {
        case Type::Undefined:
            switch (m_ubo_type) {
                case UBOType::Float:
                {
                    json["m_type"] = "Float";
                    json["m_value"] = std::any_cast<float>(m_value);
                    break;
                }
                case UBOType::Int:
                {
                    json["m_type"] = "Int";
                    json["m_value"] = std::any_cast<int>(m_value);
                    break;
                }
                case UBOType::Vec4:
                {
                    json["m_type"] = "Vec4";
                    json["m_value"] = Serialization::Json::object();
                    Serialization::Archive temp_archive(archive, &json["m_value"]);
                    Serialization::serialize(std::any_cast<glm::vec4>(m_value), temp_archive);
                    break;
                }
                case UBOType::Mat4:
                {
                    json["m_type"] = "Mat4";
                    json["m_value"] = Serialization::Json::object();
                    Serialization::Archive temp_archive(archive, &json["m_value"]);
                    Serialization::serialize(std::any_cast<glm::mat4>(m_value), temp_archive);
                    break;
                }
                case UBOType::Undefined:
                default:
                    break;
            }
            break;
        case Type::Texture:
        case Type::StorageImage:
        {
            json["m_type"] = "Texture";
            json["m_value"] = Serialization::Json::object();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::serialize(std::any_cast<std::shared_ptr<AssetRef>>(m_value), temp_archive);
            break;
        }
        case Type::UBO:
            // Ignore UBO
            break;
        case Type::StorageBuffer:
            assert(false && "Unimplemented");
        }
    }

    void MaterialProperty::load_from_archive(Serialization::Archive &archive)
    {
        Serialization::Json &json = *archive.m_cursor;
        std::string type = json["m_type"];
        std::string ubo_type = json["m_ubo_type"];
        if (type == "Undefined")
        {
            m_type = Type::Undefined;
            if (ubo_type == "Float") {
                m_ubo_type = UBOType::Float;
                m_value = json["m_value"].get<float>();
            } else if (ubo_type == "Int") {
                m_ubo_type = UBOType::Int;
                m_value = json["m_value"].get<int>();
            } else if (ubo_type == "Mat4") {
                m_ubo_type = UBOType::Mat4;
                m_value = glm::mat4{};
                Serialization::Archive temp_archive(archive, &json["m_value"]);
                Serialization::deserialize(std::any_cast<glm::mat4 &>(m_value), temp_archive);
            } else if (ubo_type == "Vec4") {
                m_ubo_type = UBOType::Vec4;
                m_value = glm::vec4{};
                Serialization::Archive temp_archive(archive, &json["m_value"]);
                Serialization::deserialize(std::any_cast<glm::vec4 &>(m_value), temp_archive);
            }
            
        }
        else if (type == "Texture")
        {
            m_type = Type::Texture;
            m_value = std::make_shared<AssetRef>();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::deserialize(std::any_cast<std::shared_ptr<AssetRef> &>(m_value), temp_archive);
        }
    }
}
