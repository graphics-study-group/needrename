#ifndef REFLECTION_SERIALIZATION_SMART_POINTER_INCLUDED
#define REFLECTION_SERIALIZATION_SMART_POINTER_INCLUDED

#include "serialization.h"
#include <memory>
#include <unordered_map>

namespace Engine {
    namespace Serialization {
        using AddressID = uintptr_t;
        using IDMap = std::unordered_map<AddressID, int>;
        using PointerMap = std::unordered_map<int, std::shared_ptr<void>>;
        static const std::string SMART_POINTER_DATA_KEY = "%smart_pointer_data";

        class SmartPointerResolver : public Resolver {
        public:
            IDMap id_map{};
            PointerMap pointer_map{};
            int current_id = 1;

            std::string GetIDKey(int id) {
                return std::string("&") + std::to_string(id);
            }
            int GetKeyIDFromString(const Json &json) {
                return std::stoi(json.get<std::string>().substr(1));
            }
        };

        template <typename T>
        void save_to_archive(const std::shared_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (value) {
                auto &resolver = archive.GetOrCreateResolver<SmartPointerResolver>();
                if (!archive.m_context->json.contains(SMART_POINTER_DATA_KEY)) {
                    archive.m_context->json[SMART_POINTER_DATA_KEY] = Json::object();
                }

                AddressID adr_id = reinterpret_cast<AddressID>(value.get());
                if (resolver.id_map.find(adr_id) == resolver.id_map.end()) {
                    resolver.id_map[adr_id] = resolver.current_id++;
                    std::string str_id = resolver.GetIDKey(resolver.id_map[adr_id]);
                    archive.m_context->json[SMART_POINTER_DATA_KEY][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json[SMART_POINTER_DATA_KEY][str_id]);
                    serialize(*value, temp_archive);
                }
                json = resolver.GetIDKey(resolver.id_map[adr_id]);
            } else {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::shared_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (!json.is_null()) {
                auto &resolver = archive.GetOrCreateResolver<SmartPointerResolver>();

                std::string str_id = json.get<std::string>();
                int id = resolver.GetKeyIDFromString(json);
                if (resolver.pointer_map.find(id) == resolver.pointer_map.end()) {
                    Archive temp_archive(archive, &archive.m_context->json[SMART_POINTER_DATA_KEY][str_id]);
                    auto type = Engine::Reflection::GetType(
                        archive.m_context->json[SMART_POINTER_DATA_KEY][str_id]["%type"].get<std::string>()
                    );
                    if (type->IsReflectable()) {
                        auto var = type->CreateInstance(SerializationMarker{});
                        resolver.pointer_map[id] = std::shared_ptr<void>(static_cast<T *>(var.GetDataPtr()));
                        var.SetNeedFree(false);
                        value = static_pointer_cast<T>(resolver.pointer_map[id]);
                        deserialize(*value, temp_archive);
                    } else {
                        value = backdoor_create_shared<T>();
                        resolver.pointer_map[id] = value;
                        deserialize(*value, temp_archive);
                    }
                } else {
                    value = static_pointer_cast<T>(resolver.pointer_map[id]);
                }
            } else {
                value = nullptr;
            }
        }

        template <typename T>
        void save_to_archive(const std::weak_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (value.lock()) {
                auto &resolver = archive.GetOrCreateResolver<SmartPointerResolver>();
                if (!archive.m_context->json.contains(SMART_POINTER_DATA_KEY)) {
                    archive.m_context->json[SMART_POINTER_DATA_KEY] = Json::object();
                }

                AddressID adr_id = reinterpret_cast<AddressID>(value.lock().get());
                if (resolver.id_map.find(adr_id) == resolver.id_map.end()) {
                    resolver.id_map[adr_id] = resolver.current_id++;
                    std::string str_id = resolver.GetIDKey(resolver.id_map[adr_id]);
                    archive.m_context->json[SMART_POINTER_DATA_KEY][str_id] = Json::object();
                    Archive temp_archive(archive, &archive.m_context->json[SMART_POINTER_DATA_KEY][str_id]);
                    serialize(*(value.lock()), temp_archive);
                }
                json = resolver.GetIDKey(resolver.id_map[adr_id]);
            } else {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::weak_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (!json.is_null()) {
                auto &resolver = archive.GetOrCreateResolver<SmartPointerResolver>();

                std::string str_id = json.get<std::string>();
                int id = resolver.GetKeyIDFromString(json);
                if (resolver.pointer_map.find(id) == resolver.pointer_map.end()) {
                    Archive temp_archive(archive, &archive.m_context->json[SMART_POINTER_DATA_KEY][str_id]);
                    auto type = Engine::Reflection::GetType(
                        archive.m_context->json[SMART_POINTER_DATA_KEY][str_id]["%type"].get<std::string>()
                    );
                    if (type->IsReflectable()) {
                        auto var = type->CreateInstance(SerializationMarker{});
                        resolver.pointer_map[id] = std::shared_ptr<void>(static_cast<T *>(var.GetDataPtr()));
                        var.SetNeedFree(false);
                        value = static_pointer_cast<T>(resolver.pointer_map[id]);
                        deserialize(*(value.lock()), temp_archive);
                    } else {
                        value = backdoor_create_shared<T>();
                        resolver.pointer_map[id] = value.lock();
                        deserialize(*(value.lock()), temp_archive);
                    }
                } else {
                    value = static_pointer_cast<T>(resolver.pointer_map[id]);
                }
            } else {
                value.reset();
            }
        }

        template <typename T>
        void save_to_archive(const std::unique_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (value) {
                Archive temp_archive(archive, &json);
                serialize(*value, temp_archive);
            } else {
                json = nullptr;
            }
        }

        template <typename T>
        void load_from_archive(std::unique_ptr<T> &value, Archive &archive) {
            Json &json = *archive.m_cursor;
            if (!json.is_null()) {
                Archive temp_archive(archive, &json);
                std::shared_ptr<const Engine::Reflection::Type> type{};
                if (json.contains("%type")) type = Engine::Reflection::GetType(json["%type"].get<std::string>());
                if (type && type->IsReflectable()) {
                    auto var = type->CreateInstance(SerializationMarker{});
                    value = std::unique_ptr<T>(static_cast<T *>(var.GetDataPtr()));
                    var.SetNeedFree(false);
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
