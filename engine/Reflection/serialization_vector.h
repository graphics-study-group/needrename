#ifndef REFLECTION_SERIALIZATION_VECTOR_INCLUDED
#define REFLECTION_SERIALIZATION_VECTOR_INCLUDED

#include <vector>
#include "serialization.h"

namespace Engine
{
    namespace Serialization
    {
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
    }
}

#endif // REFLECTION_SERIALIZATION_VECTOR_INCLUDED
