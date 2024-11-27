#ifndef REFLECTION_ARCHIVE_INCLUDED
#define REFLECTION_ARCHIVE_INCLUDED

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <nlohmann/json.hpp>

namespace Engine
{
    namespace Serialization
    {
        using Json = nlohmann::json;
        using Buffer = std::vector<std::byte>;
        using BufferNamePair = std::pair<std::string, Buffer>;
        using BufferList = std::vector<BufferNamePair>;
        using AddressID = unsigned long long;
        using IDMap = std::unordered_map<AddressID, int>;
        using PointerMap = std::unordered_map<int, void *>;

        /// @brief An archive class is used to store serialized data
        /// @details TODO: Add more details
        class Archive
        {
        public:
            struct GlobalContext
            {
                Json json{};
                BufferList extra_datas{};
                IDMap id_map{};
                int current_id = 0;
                PointerMap pointer_map{};
                bool initialized = false;
            };

        public:
            Archive() = default;
            ~Archive() = default;
            Archive(const Archive &, Json *cursor = nullptr);

        public:
            std::shared_ptr<GlobalContext> m_context = std::make_shared<GlobalContext>();
            Json *m_cursor;

        public:
            void init(const std::string &archive_type_name, const void *main_data = nullptr);
            void load_init(void *main_data);
            void clear();
            void save_to_file(const std::filesystem::path &path);
        };
    }
}

#endif // REFLECTION_ARCHIVE_INCLUDED
