#ifndef REFLECTION_SERIALIZATION_UNORDERED_MAP_INCLUDED
#define REFLECTION_SERIALIZATION_UNORDERED_MAP_INCLUDED

#include <unordered_map>
#include <string>
#include "serialization.h"

namespace Engine
{
    namespace Serialization
    {
        template <typename T>
        void save_to_archive(const std::unordered_map<std::string, T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json = Json::object();
            for (auto &item : value)
            {
                if constexpr (is_basic_type<T>)
                {
                    json[item.first] = item.second;
                }
                else
                {
                    json[item.first] = Json::object();
                    Archive temp_archive(archive, &json[item.first]);
                    serialize(item.second, temp_archive);
                }
            }
        }

        template <typename T>
        void load_from_archive(std::unordered_map<std::string, T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            value.clear();
            for (auto &item : json.items())
            {
                if constexpr (is_basic_type<T>)
                {
                    value[item.key()] = item.value().get<T>();
                }
                else
                {
                    Archive temp_archive(archive, &item.value());
                    value[item.key()] = T();
                    deserialize(value[item.key()], temp_archive);
                }
            }
        }

        template <typename T>
        concept not_string_un_map = !std::is_same<std::remove_cvref<T>, std::string>::value;

        template <not_string_un_map key_type, typename T>
        void save_to_archive(const std::unordered_map<key_type, T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json = Json::array();
            for (auto &item : value)
            {
                Json temp_json = Json::object();
                if constexpr (is_basic_type<key_type>)
                {
                    temp_json["key"] = item.first;
                }
                else
                {
                    temp_json["key"] = Json::object();
                    Archive temp_archive(archive, &temp_json["key"]);
                    serialize(item.first, temp_archive);
                }
                if constexpr (is_basic_type<T>)
                {
                    temp_json["value"] = item.second;
                }
                else
                {
                    temp_json["value"] = Json::object();
                    Archive temp_archive(archive, &temp_json["value"]);
                    serialize(item.second, temp_archive);
                }
                json.push_back(temp_json);
            }
        }

        template <not_string_un_map key_type, typename T>
        void load_from_archive(std::unordered_map<key_type, T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            value.clear();
            for (auto &item : json)
            {
                if constexpr (is_basic_type<key_type>)
                {
                    key_type key = item["key"].get<key_type>();
                    if constexpr (is_basic_type<T>)
                    {
                        value[key] = item["value"].get<T>();
                    }
                    else
                    {
                        Archive temp_archive(archive, &item["value"]);
                        value[key] = T();
                        deserialize(value[key], temp_archive);
                    }
                }
                else
                {
                    Archive temp_archive(archive, &item["key"]);
                    key_type key;
                    deserialize(key, temp_archive);
                    if constexpr (is_basic_type<T>)
                    {
                        value[key] = item["value"].get<T>();
                    }
                    else
                    {
                        Archive temp_archive(archive, &item["value"]);
                        value[key] = T();
                        deserialize(value[key], temp_archive);
                    }
                }
            }
        }
    }
}

#endif // REFLECTION_SERIALIZATION_UNORDERED_MAP_INCLUDED
