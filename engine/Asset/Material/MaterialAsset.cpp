#include "MaterialAsset.h"
#include <Asset/Texture/Image2DTextureAsset.h>

namespace Engine
{
    MaterialProperty::MaterialProperty(float value)
        : m_type(Type::Float), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(int value)
        : m_type(Type::Int), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(const glm::vec4 &value)
        : m_type(Type::Vec4), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(const glm::mat4 &value)
        : m_type(Type::Mat4), m_value(value)
    {
    }

    MaterialProperty::MaterialProperty(const std::shared_ptr<TextureAsset> &value)
        : m_type(Type::Texture), m_value(value)
    {
    }

    void MaterialProperty::save_to_archive(Serialization::Archive &archive) const
    {
        Serialization::Json &json = *archive.m_cursor;
        switch (m_type)
        {
        case Type::Float:
        {
            json["m_type"] = "Float";
            json["m_value"] = std::any_cast<float>(m_value);
            break;
        }
        case Type::Int:
        {
            json["m_type"] = "Int";
            json["m_value"] = std::any_cast<int>(m_value);
            break;
        }
        case Type::Vec4:
        {
            json["m_type"] = "Vec4";
            json["m_value"] = Serialization::Json::object();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::serialize(std::any_cast<glm::vec4>(m_value), temp_archive);
            break;
        }
        case Type::Mat4:
        {
            json["m_type"] = "Mat4";
            json["m_value"] = Serialization::Json::object();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::serialize(std::any_cast<glm::mat4>(m_value), temp_archive);
            break;
        }
        case Type::Texture:
        {
            json["m_type"] = "Texture";
            json["m_value"] = Serialization::Json::object();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::serialize(std::any_cast<std::shared_ptr<TextureAsset>>(m_value), temp_archive);
            break;
        }
        }
    }

    void MaterialProperty::load_from_archive(Serialization::Archive &archive)
    {
        Serialization::Json &json = *archive.m_cursor;
        std::string type = json["m_type"];
        if (type == "Float")
        {
            m_type = Type::Float;
            m_value = json["m_value"].get<float>();
        }
        else if (type == "Int")
        {
            m_type = Type::Int;
            m_value = json["m_value"].get<int>();
        }
        else if (type == "Vec4")
        {
            m_type = Type::Vec4;
            m_value = glm::vec4{};
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::deserialize(std::any_cast<glm::vec4 &>(m_value), temp_archive);
        }
        else if (type == "Mat4")
        {
            m_type = Type::Mat4;
            m_value = glm::mat4{};
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::deserialize(std::any_cast<glm::mat4 &>(m_value), temp_archive);
        }
        else if (type == "Texture")
        {
            m_type = Type::Texture;
            m_value = std::make_shared<TextureAsset>();
            Serialization::Archive temp_archive(archive, &json["m_value"]);
            Serialization::deserialize(std::any_cast<std::shared_ptr<TextureAsset> &>(m_value), temp_archive);
        }
    }
}
