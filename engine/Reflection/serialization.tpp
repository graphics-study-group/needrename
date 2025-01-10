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
            json["%type"] = typeid(T).name();
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
                        assert(type->m_type_info == nullptr || type->m_type_info->name() == typeid(T).name());
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
                        assert(type->m_type_info == nullptr || type->m_type_info->name() == typeid(T).name());
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
                    assert(type->m_type_info == nullptr || type->m_type_info->name() == typeid(T).name());
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
