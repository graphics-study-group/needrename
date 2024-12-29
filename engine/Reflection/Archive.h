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
        using AddressID = unsigned long long;
        using IDMap = std::unordered_map<AddressID, int>;
        using PointerMap = std::unordered_map<int, std::shared_ptr<void>>;

        /// @brief An archive class is used to store serialized data
        /// @details TODO: Add more details
        class Archive
        {
        public:
            struct GlobalContext
            {
                Json json{};
                Buffer extra_data{};

                IDMap id_map{};
                int current_id = 0;

                PointerMap pointer_map{};

                bool save_prepared = false;
                bool load_prepared = false;
            };

        public:
            Archive() = default;
            ~Archive() = default;
            Archive(const Archive &, Json *cursor = nullptr);

        public:
            std::shared_ptr<GlobalContext> m_context = std::make_shared<GlobalContext>();
            Json *m_cursor;

        public:
            void prepare_save(std::shared_ptr<const void> main_data = nullptr);
            void prepare_load(std::shared_ptr<void> main_data = nullptr);
            void clear();
            void save_to_file(std::filesystem::path path);
            void load_from_file(std::filesystem::path path);

            template<typename T>
            const Json &GetMainDataProperty(const T& property) const
            {
                auto &json = m_context->json;
                assert(json.contains("%main_id"));
                std::string str_id = json["%main_id"].get<std::string>();
                assert(json["%data"].contains(str_id));
                assert(json["%data"][str_id].contains(property));
                return json["%data"][str_id][property];
            }
        };
    }
}

#endif // REFLECTION_ARCHIVE_INCLUDED
