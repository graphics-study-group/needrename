#include "serialization.h"
#include "reflection.h"

namespace Engine
{
    namespace Serialization
    {
        template <is_basic_type T>
        void save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json["%type"] = Engine::Reflection::GetType(std::type_index(typeid(T)))->m_name;
            json["data"] = value;
        }

        template <is_basic_type T>
        void load_from_archive(T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            value = json["data"].get<T>();
        }

        template <is_array_type T>
        void save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json = Json::array();
            for (auto &item : value)
            {
                if constexpr (is_basic_type<std::remove_const_t<std::remove_reference_t<decltype(value[0])>>>)
                {
                    json.push_back(item);
                }
                else
                {
                    json.push_back(Json::object());
                    Archive temp_archive(archive, &json.back());
                    serialize(item, temp_archive);
                }
            }
        }

        template <is_array_type T>
        void load_from_archive(T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            int i = 0;
            for (auto &item : json)
            {
                if constexpr (is_basic_type<std::remove_reference_t<decltype(value[0])>>)
                {
                    value[i] = item.get<std::remove_reference_t<decltype(value[0])>>();
                }
                else
                {
                    Archive temp_archive(archive, &item);
                    deserialize(value[i], temp_archive);
                }
                i++;
            }
        }

        template <is_enum_type T>
        void save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json = static_cast<int>(value);
        }

        template <is_enum_type T>
        void load_from_archive(T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            value = static_cast<T>(json.get<int>());
        }

        template <is_std_any T>
        void save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (value.has_value())
            {
                auto &type_info = value.type();
                if (type_info == typeid(std::nullptr_t))
                {
                    json["%type"] = Engine::Reflection::GetType("std::nullptr_t")->m_name;
                    json["data"] = nullptr;
                }
                else if (type_info == typeid(bool))
                {
                    json["%type"] = Engine::Reflection::GetType("bool")->m_name;
                    json["data"] = std::any_cast<bool>(value);
                }
                else if (type_info == typeid(char))
                {
                    json["%type"] = Engine::Reflection::GetType("char")->m_name;
                    json["data"] = std::any_cast<char>(value);
                }
                else if (type_info == typeid(signed char))
                {
                    json["%type"] = Engine::Reflection::GetType("signed char")->m_name;
                    json["data"] = std::any_cast<signed char>(value);
                }
                else if (type_info == typeid(unsigned char))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned char")->m_name;
                    json["data"] = std::any_cast<unsigned char>(value);
                }
                else if (type_info == typeid(char8_t))
                {
                    json["%type"] = Engine::Reflection::GetType("char8_t")->m_name;
                    json["data"] = std::any_cast<char8_t>(value);
                }
                else if (type_info == typeid(char16_t))
                {
                    json["%type"] = Engine::Reflection::GetType("char16_t")->m_name;
                    json["data"] = std::any_cast<char16_t>(value);
                }
                else if (type_info == typeid(char32_t))
                {
                    json["%type"] = Engine::Reflection::GetType("char32_t")->m_name;
                    json["data"] = std::any_cast<char32_t>(value);
                }
                else if (type_info == typeid(wchar_t))
                {
                    json["%type"] = Engine::Reflection::GetType("wchar_t")->m_name;
                    json["data"] = std::any_cast<wchar_t>(value);
                }
                else if (type_info == typeid(short))
                {
                    json["%type"] = Engine::Reflection::GetType("short")->m_name;
                    json["data"] = std::any_cast<short>(value);
                }
                else if (type_info == typeid(unsigned short))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned short")->m_name;
                    json["data"] = std::any_cast<unsigned short>(value);
                }
                else if (type_info == typeid(int))
                {
                    json["%type"] = Engine::Reflection::GetType("int")->m_name;
                    json["data"] = std::any_cast<int>(value);
                }
                else if (type_info == typeid(unsigned int))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned int")->m_name;
                    json["data"] = std::any_cast<unsigned int>(value);
                }
                else if (type_info == typeid(long))
                {
                    json["%type"] = Engine::Reflection::GetType("long")->m_name;
                    json["data"] = std::any_cast<long>(value);
                }
                else if (type_info == typeid(unsigned long))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned long")->m_name;
                    json["data"] = std::any_cast<unsigned long>(value);
                }
                else if (type_info == typeid(long long))
                {
                    json["%type"] = Engine::Reflection::GetType("long long")->m_name;
                    json["data"] = std::any_cast<long long>(value);
                }
                else if (type_info == typeid(unsigned long long))
                {
                    json["%type"] = Engine::Reflection::GetType("unsigned long long")->m_name;
                    json["data"] = std::any_cast<unsigned long long>(value);
                }
                else if (type_info == typeid(float))
                {
                    json["%type"] = Engine::Reflection::GetType("float")->m_name;
                    json["data"] = std::any_cast<float>(value);
                }
                else if (type_info == typeid(double))
                {
                    json["%type"] = Engine::Reflection::GetType("double")->m_name;
                    json["data"] = std::any_cast<double>(value);
                }
                else if (type_info == typeid(long double))
                {
                    json["%type"] = Engine::Reflection::GetType("long double")->m_name;
                    json["data"] = std::any_cast<long double>(value);
                }
                else if (type_info == typeid(std::string))
                {
                    json["%type"] = Engine::Reflection::GetType("std::string")->m_name;
                    json["data"] = std::any_cast<std::string>(value);
                }
                else
                {
                    throw std::runtime_error("Unsupported type for std::any serialization: " + std::string(type_info.name()));
                }
            }
            else
            {
                json = nullptr;
            }
        }

        template <is_std_any T>
        void load_from_archive(T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (!json.is_null())
            {
                auto type_name = json["%type"].get<std::string>();
                if (type_name == "std::nullptr_t")
                {
                    value = nullptr;
                }
                else if (type_name == "bool")
                {
                    value = json["data"].get<bool>();
                }
                else if (type_name == "char")
                {
                    value = json["data"].get<char>();
                }
                else if (type_name == "signed char")
                {
                    value = json["data"].get<signed char>();
                }
                else if (type_name == "unsigned char")
                {
                    value = json["data"].get<unsigned char>();
                }
                else if (type_name == "char8_t")
                {
                    value = json["data"].get<char8_t>();
                }
                else if (type_name == "char16_t")
                {
                    value = json["data"].get<char16_t>();
                }
                else if (type_name == "char32_t")
                {
                    value = json["data"].get<char32_t>();
                }
                else if (type_name == "wchar_t")
                {
                    value = json["data"].get<wchar_t>();
                }
                else if (type_name == "short")
                {
                    value = json["data"].get<short>();
                }
                else if (type_name == "unsigned short")
                {
                    value = json["data"].get<unsigned short>();
                }
                else if (type_name == "int")
                {
                    value = json["data"].get<int>();
                }
                else if (type_name == "unsigned int")
                {
                    value = json["data"].get<unsigned int>();
                }
                else if (type_name == "long")
                {
                    value = json["data"].get<long>();
                }
                else if (type_name == "unsigned long")
                {
                    value = json["data"].get<unsigned long>();
                }
                else if (type_name == "long long")
                {
                    value = json["data"].get<long long>();
                }
                else if (type_name == "unsigned long long")
                {
                    value = json["data"].get<unsigned long long>();
                }
                else if (type_name == "float")
                {
                    value = json["data"].get<float>();
                }
                else if (type_name == "double")
                {
                    value = json["data"].get<double>();
                }
                else if (type_name == "long double")
                {
                    value = json["data"].get<long double>();
                }
                else if (type_name == "std::string")
                {
                    value = json["data"].get<std::string>();
                }
                else
                {
                    throw std::runtime_error("Unsupported type for std::any deserialization: " + type_name);
                }
            }
            else
            {
                value.reset();
            }
        }

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
        void save_to_archive(const std::map<std::string, T> &value, Archive &archive)
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
        void load_from_archive(std::map<std::string, T> &value, Archive &archive)
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

        template <not_string key_type, typename T>
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

        template <not_string key_type, typename T>
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

        template <not_string key_type, typename T>
        void save_to_archive(const std::map<key_type, T> &value, Archive &archive)
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

        template <not_string key_type, typename T>
        void load_from_archive(std::map<key_type, T> &value, Archive &archive)
        {

        }

        template <typename T>
        void save_to_archive(const std::vector<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json = Json::array();
            for (auto &item : value)
            {
                if constexpr (is_basic_type<T>)
                {
                    json.push_back(item);
                }
                else
                {
                    json.push_back(Json::object());
                    Archive temp_archive(archive, &json.back());
                    serialize(item, temp_archive);
                }
            }
        }

        template <typename T>
        void load_from_archive(std::vector<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            value.clear();
            for (auto &item : json)
            {
                if constexpr (is_basic_type<T>)
                {
                    value.push_back(item.get<T>());
                }
                else
                {
                    Archive temp_archive(archive, &item);
                    value.emplace_back();
                    deserialize(value.back(), temp_archive);
                }
            }
        }

        template <typename T>
        void save_to_archive(const std::shared_ptr<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (value)
            {
                AddressID adr_id = reinterpret_cast<AddressID>(value.get());
                if (archive.m_context->id_map.find(adr_id) == archive.m_context->id_map.end())
                {
                    archive.m_context->id_map[adr_id] = archive.m_context->current_id++;
                    std::string str_id = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
                    archive.m_context->json["%data"][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    serialize(*value, temp_archive);
                }
                json = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
            }
            else
            {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::shared_ptr<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (!json.is_null())
            {
                std::string str_id = json.get<std::string>();
                int id = std::stoi(str_id.substr(1));
                if (archive.m_context->pointer_map.find(id) == archive.m_context->pointer_map.end())
                {
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    auto type = Engine::Reflection::GetType(archive.m_context->json["%data"][str_id]["%type"].get<std::string>());
                    if (type->m_reflectable)
                    {
                        auto var = type->CreateInstance(SerializationMarker{});
                        archive.m_context->pointer_map[id] = std::shared_ptr<void>(static_cast<T *>(var.GetDataPtr()));
                        value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                        deserialize(*value, temp_archive);
                    }
                    else
                    {
                        value = backdoor_create_shared<T>();
                        archive.m_context->pointer_map[id] = value;
                        deserialize(*value, temp_archive);
                    }
                }
                else
                {
                    value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                }
            }
            else
            {
                value = nullptr;
            }
        }

        template <typename T>
        void save_to_archive(const std::weak_ptr<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (value.lock())
            {
                AddressID adr_id = reinterpret_cast<AddressID>(value.lock().get());
                if (archive.m_context->id_map.find(adr_id) == archive.m_context->id_map.end())
                {
                    archive.m_context->id_map[adr_id] = archive.m_context->current_id++;
                    std::string str_id = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
                    archive.m_context->json["%data"][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    serialize(*(value.lock()), temp_archive);
                }
                json = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
            }
            else
            {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::weak_ptr<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (!json.is_null())
            {
                std::string str_id = json.get<std::string>();
                int id = std::stoi(str_id.substr(1));
                if (archive.m_context->pointer_map.find(id) == archive.m_context->pointer_map.end())
                {
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    auto type = Engine::Reflection::GetType(archive.m_context->json["%data"][str_id]["%type"].get<std::string>());
                    if (type->m_reflectable)
                    {
                        auto var = type->CreateInstance(Serialization::SerializationMarker{});
                        archive.m_context->pointer_map[id] = std::shared_ptr<void>(static_cast<T *>(var.GetDataPtr()));
                        value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                        deserialize(*(value.lock()), temp_archive);
                    }
                    else
                    {
                        value = backdoor_create_shared<T>();
                        archive.m_context->pointer_map[id] = value.lock();
                        deserialize(*(value.lock()), temp_archive);
                    }
                }
                else
                {
                    value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                }
            }
            else
            {
                value.reset();
            }
        }

        template <typename T>
        void save_to_archive(const std::unique_ptr<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (value)
            {
                AddressID adr_id = reinterpret_cast<AddressID>(value.get());
                if (archive.m_context->id_map.find(adr_id) == archive.m_context->id_map.end())
                {
                    archive.m_context->id_map[adr_id] = archive.m_context->current_id++;
                    std::string str_id = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
                    archive.m_context->json["%data"][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    serialize(*value, temp_archive);
                }
                json = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
            }
            else
            {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::unique_ptr<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (!json.is_null())
            {
                std::string str_id = json.get<std::string>();
                // int id = std::stoi(str_id.substr(1));

                Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                auto type = Engine::Reflection::GetType(archive.m_context->json["%data"][str_id]["%type"].get<std::string>());
                if (type->m_reflectable)
                {
                    auto var = type->CreateInstance(Serialization::SerializationMarker{});
                    value = std::unique_ptr<T>(static_cast<T *>(var.GetDataPtr()));
                    deserialize(*value, temp_archive);
                }
                else
                {
                    value = backdoor_create_unique<T>();
                    deserialize(*value, temp_archive);
                }
            }
            else
            {
                value = nullptr;
            }
        }
    }
}
