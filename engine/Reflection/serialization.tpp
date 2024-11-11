#include "serialization.h"

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

        template <typename T>
        void save(const std::weak_ptr<T>& value, Archive& buffer)
        {
            Json &json = buffer.json;
            if (auto shared = value.lock())
            {
                Archive temp_buffer;
                serialize(*shared, temp_buffer);
                json = temp_buffer.json;
                buffer.buffers.insert(buffer.buffers.end(), temp_buffer.buffers.begin(), temp_buffer.buffers.end());
            }
        }

        template <typename T>
        void load(std::vector<T>& value, Archive& buffer)
        {
            const Json &json = buffer.json;
            value.clear();
            for (const auto& item : json)
            {
                Archive temp_buffer;
                temp_buffer.json = item;
                throw std::runtime_error("Not implemented");
                // T temp;
                // deserialize(temp, temp_buffer);
                // value.push_back(temp);
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
                throw std::runtime_error("Not implemented");
                // T temp;
                // deserialize(temp, temp_buffer);
                // value = std::make_shared<T>(temp);
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
                throw std::runtime_error("Not implemented");
                // T temp;
                // deserialize(temp, temp_buffer);
                // value = std::make_unique<T>(temp);
            }
        }

        template <typename T>
        void load(std::weak_ptr<T>& value, Archive& buffer)
        {
            const Json &json = buffer.json;
            if (!json.is_null())
            {
                Archive temp_buffer;
                temp_buffer.json = json;
                throw std::runtime_error("Not implemented");
                // T temp;
                // deserialize(temp, temp_buffer);
                // value = std::make_shared<T>(temp);
            }
        }
    }
}
