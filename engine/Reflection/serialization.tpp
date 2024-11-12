#include "serialization.h"
#include "reflection.h"

namespace Engine
{
    namespace Serialization
    {
        template <typename T>
        void save(const std::vector<T>& value, Archive& buffer)
        {
            Json &json = buffer.json;
            json = Json::array();
            for (const auto& item : value)
            {
                Archive temp_buffer;
                serialize(item, temp_buffer);
                json.push_back(temp_buffer.json);
                buffer.buffers.insert(buffer.buffers.end(), temp_buffer.buffers.begin(), temp_buffer.buffers.end());
            }
        }

        template <typename T>
        void save(const std::shared_ptr<T>& value, Archive& buffer)
        {
            Json &json = buffer.json;
            if (value)
            {
                Archive temp_buffer;
                serialize(*value, temp_buffer);
                json = temp_buffer.json;
                buffer.buffers.insert(buffer.buffers.end(), temp_buffer.buffers.begin(), temp_buffer.buffers.end());
            }
        }

        template <typename T>
        void save(const std::unique_ptr<T>& value, Archive& buffer)
        {
            Json &json = buffer.json;
            if (value)
            {
                Archive temp_buffer;
                serialize(*value, temp_buffer);
                json = temp_buffer.json;
                buffer.buffers.insert(buffer.buffers.end(), temp_buffer.buffers.begin(), temp_buffer.buffers.end());
            }
        }

        // TODO: Implement load_and_construct for class T
        template <typename T>
        void load(std::vector<T>& value, Archive& buffer)
        {
            const Json &json = buffer.json;
            value.clear();
            for (const auto& item : json)
            {
                Archive temp_buffer;
                temp_buffer.json = item;
                value.emplace_back();
                deserialize(value.back(), temp_buffer);
            }
        }

        template <typename T>
        void load(std::shared_ptr<T>& value, Archive& buffer)
        {
            const Json &json = buffer.json;
            if (!json.is_null())
            {
                Archive temp_buffer;
                temp_buffer.json = json;
                auto type = Engine::Reflection::GetType(json["__type"]);
                auto var = type->CreateInstance();
                value = std::shared_ptr<T>(static_cast<T*>(var.GetDataPtr()));
                deserialize(*value, temp_buffer);
            }
        }

        template <typename T>
        void load(std::unique_ptr<T>& value, Archive& buffer)
        {
            const Json &json = buffer.json;
            if (!json.is_null())
            {
                Archive temp_buffer;
                temp_buffer.json = json;
                auto type = Engine::Reflection::GetType(json["__type"]);
                auto var = type->CreateInstance();
                value = std::unique_ptr<T>(static_cast<T*>(var.GetDataPtr()));
                deserialize(*value, temp_buffer);
            }
        }
    }
}
