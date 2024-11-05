#include <vector>
#include <nlohmann/json.hpp>

namespace Engine
{
    namespace Serialization
    {
        using Json = nlohmann::json;
        using SerializedFile = std::pair<std::string, std::byte *>; // filename, data

        template <typename T>
        void save(const T& value, std::vector<SerializedFile>& data)
        {
            
        }

        template <typename T>
        void load(const T& value, std::vector<SerializedFile>& data)
        {

        }
    }
}
