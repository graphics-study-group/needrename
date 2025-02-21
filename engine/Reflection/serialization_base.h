#ifndef REFLECTION_SERIALIZATION_BASE_INCLUDED
#define REFLECTION_SERIALIZATION_BASE_INCLUDED

#include "serialization.h"
#include "reflection.h"

namespace Engine
{
    namespace Serialization
    {
        /// @brief concept to check if a type is a basic type which json can serialize
        template <typename T>
        concept is_basic_type = std::disjunction<std::is_integral<T>, std::is_floating_point<T>, std::is_same<T, std::string>, std::is_same<T, bool>>::value;
        /// @brief concept to check if a type is an array
        template <typename T>
        concept is_array_type = std::is_array<T>::value;
        /// @brief concept to check if a type is an enum
        template <typename T>
        concept is_enum_type = std::is_enum<T>::value;

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
    }
}

#endif // REFLECTION_SERIALIZATION_BASE_INCLUDED
