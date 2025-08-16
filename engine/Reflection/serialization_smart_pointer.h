#ifndef REFLECTION_SERIALIZATION_SMART_POINTER_INCLUDED
#define REFLECTION_SERIALIZATION_SMART_POINTER_INCLUDED

#include "serialization.h"
#include <memory>

namespace Engine {
    namespace Serialization {
        template <typename T>
        void save_to_archive(const std::shared_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (value) {
                AddressID adr_id = reinterpret_cast<AddressID>(value.get());
                if (archive.m_context->id_map.find(adr_id) == archive.m_context->id_map.end()) {
                    archive.m_context->id_map[adr_id] = archive.m_context->current_id++;
                    std::string str_id = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
                    archive.m_context->json["%data"][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    serialize(*value, temp_archive);
                }
                json = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
            } else {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::shared_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (!json.is_null()) {
                std::string str_id = json.get<std::string>();
                int id = std::stoi(str_id.substr(1));
                if (archive.m_context->pointer_map.find(id) == archive.m_context->pointer_map.end()) {
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    auto type = Engine::Reflection::GetType(
                        archive.m_context->json["%data"][str_id]["%type"].get<std::string>()
                    );
                    if (type->IsReflectable()) {
                        auto var = type->CreateInstance(SerializationMarker{});
                        archive.m_context->pointer_map[id] = std::shared_ptr<void>(static_cast<T *>(var.GetDataPtr()));
                        value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                        deserialize(*value, temp_archive);
                    } else {
                        value = backdoor_create_shared<T>();
                        archive.m_context->pointer_map[id] = value;
                        deserialize(*value, temp_archive);
                    }
                } else {
                    value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                }
            } else {
                value = nullptr;
            }
        }

        template <typename T>
        void save_to_archive(const std::weak_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (value.lock()) {
                AddressID adr_id = reinterpret_cast<AddressID>(value.lock().get());
                if (archive.m_context->id_map.find(adr_id) == archive.m_context->id_map.end()) {
                    archive.m_context->id_map[adr_id] = archive.m_context->current_id++;
                    std::string str_id = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
                    archive.m_context->json["%data"][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    serialize(*(value.lock()), temp_archive);
                }
                json = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
            } else {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::weak_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (!json.is_null()) {
                std::string str_id = json.get<std::string>();
                int id = std::stoi(str_id.substr(1));
                if (archive.m_context->pointer_map.find(id) == archive.m_context->pointer_map.end()) {
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    auto type = Engine::Reflection::GetType(
                        archive.m_context->json["%data"][str_id]["%type"].get<std::string>()
                    );
                    if (type->IsReflectable()) {
                        auto var = type->CreateInstance(Serialization::SerializationMarker{});
                        archive.m_context->pointer_map[id] = std::shared_ptr<void>(static_cast<T *>(var.GetDataPtr()));
                        value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                        deserialize(*(value.lock()), temp_archive);
                    } else {
                        value = backdoor_create_shared<T>();
                        archive.m_context->pointer_map[id] = value.lock();
                        deserialize(*(value.lock()), temp_archive);
                    }
                } else {
                    value = static_pointer_cast<T>(archive.m_context->pointer_map[id]);
                }
            } else {
                value.reset();
            }
        }

        template <typename T>
        void save_to_archive(const std::unique_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (value) {
                AddressID adr_id = reinterpret_cast<AddressID>(value.get());
                if (archive.m_context->id_map.find(adr_id) == archive.m_context->id_map.end()) {
                    archive.m_context->id_map[adr_id] = archive.m_context->current_id++;
                    std::string str_id = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
                    archive.m_context->json["%data"][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                    serialize(*value, temp_archive);
                }
                json = std::string("&") + std::to_string(archive.m_context->id_map[adr_id]);
            } else {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::unique_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (!json.is_null()) {
                std::string str_id = json.get<std::string>();
                // int id = std::stoi(str_id.substr(1));

                Archive temp_archive(archive, &archive.m_context->json["%data"][str_id]);
                auto type =
                    Engine::Reflection::GetType(archive.m_context->json["%data"][str_id]["%type"].get<std::string>());
                if (type->IsReflectable()) {
                    auto var = type->CreateInstance(Serialization::SerializationMarker{});
                    value = std::unique_ptr<T>(static_cast<T *>(var.GetDataPtr()));
                    deserialize(*value, temp_archive);
                } else {
                    value = backdoor_create_unique<T>();
                    deserialize(*value, temp_archive);
                }
            } else {
                value = nullptr;
            }
        }
    } // namespace Serialization
} // namespace Engine

#endif // REFLECTION_SERIALIZATION_SMART_POINTER_INCLUDED
