#include "serialization.h"
#include "reflection.h"

namespace Engine
{
    namespace Serialization
    {
        template <typename T>
        typename std::enable_if<is_basic_type<T>::value, void>::type
        save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json["%type"] = typeid(T).name();
            json["data"] = value;
        }

        template <typename T>
        typename std::enable_if<is_basic_type<T>::value, void>::type
        load_from_archive(T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            value = json["data"].get<T>();
        }

        template <typename T>
        typename std::enable_if<std::is_pointer<T>::value, void>::type
        save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            if (value != nullptr)
            {
                AddressID adr_id = reinterpret_cast<AddressID>(value);
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
        typename std::enable_if<std::is_pointer<T>::value, void>::type
        load_from_archive(T &value, Archive &archive)
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
                        Engine::Reflection::Var var = type->CreateInstance();
                        value = static_cast<T>(var.GetDataPtr());
                        archive.m_context->pointer_map[id] = value;
                        deserialize(*value, temp_archive);
                    }
                    else
                    {
                        assert(type->m_type_info == nullptr || type->m_type_info->name() == typeid(*value).name());
                        value = new (std::remove_pointer_t<T>)();
                        archive.m_context->pointer_map[id] = value;
                        deserialize(*value, temp_archive);
                    }
                }
                else
                {
                    value = static_cast<T>(archive.m_context->pointer_map[id]);
                }
            }
            else
            {
                value = nullptr;
            }
        }

        template <typename T>
        typename std::enable_if<std::is_array<T>::value, void>::type
        save_to_archive(const T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json = Json::array();
            for (auto &item : value)
            {
                if constexpr (is_basic_type<std::remove_const_t<std::remove_reference_t<decltype(value[0])>>>::value)
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
        typename std::enable_if<std::is_array<T>::value, void>::type
        load_from_archive(T &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            int i = 0;
            for (auto &item : json)
            {
                if constexpr (is_basic_type<std::remove_reference_t<decltype(value[0])>>::value)
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

        template <typename T>
        void save_to_archive(const std::vector<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            json = Json::array();
            for (auto &item : value)
            {
                if constexpr (is_basic_type<T>::value)
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

        // TODO: Implement load_and_construct for class T
        template <typename T>
        void load_from_archive(std::vector<T> &value, Archive &archive)
        {
            Json &json = *archive.m_cursor;
            value.clear();
            for (auto &item : json)
            {
                if constexpr (is_basic_type<T>::value)
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
                        auto var = type->CreateInstance();
                        archive.m_context->pointer_map[id] = var.GetDataPtr();
                        value = std::shared_ptr<T>(static_cast<T *>(var.GetDataPtr()));
                        deserialize(*value, temp_archive);
                    }
                    else
                    {
                        value = std::make_shared<T>();
                        archive.m_context->pointer_map[id] = value.get();
                        assert(type->m_type_info == nullptr || type->m_type_info->name() == typeid(T).name());
                        deserialize(*value, temp_archive);
                    }
                }
                else
                {
                    value = std::shared_ptr<T>(static_cast<T *>(archive.m_context->pointer_map[id]));
                }
            }
            else
            {
                value = nullptr;
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
                int id = std::stoi(str_id.substr(1));
                if (archive.m_context->pointer_map.find(id) == archive.m_context->pointer_map.end())
                {
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    auto type = Engine::Reflection::GetType(archive.m_context->json["%data"][str_id]["%type"].get<std::string>());
                    if (type->m_reflectable)
                    {
                        auto var = type->CreateInstance();
                        value = std::unique_ptr<T>(static_cast<T *>(var.GetDataPtr()));
                        deserialize(*value, temp_archive);
                    }
                    else
                    {
                        value = std::make_unique<T>();
                        assert(type->m_type_info == nullptr || type->m_type_info->name() == typeid(T).name());
                        deserialize(*value, temp_archive);
                    }
                }
                else
                {
                    value = std::unique_ptr<T>(static_cast<T *>(archive.m_context->pointer_map[id]));
                }
            }
            else
            {
                value = nullptr;
            }
        }
    }
}
