#ifndef REFLECTION_ARCHIVE_INCLUDED
#define REFLECTION_ARCHIVE_INCLUDED

#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace Engine {
    namespace Serialization {
        using Json = nlohmann::json;
        using BinData = std::vector<std::byte>;

        class Resolver {
        public:
            Resolver() = default;
            virtual ~Resolver() = default;
        };

        template <typename T>
        concept ResolverType = std::is_base_of_v<Resolver, T>;

        /// @brief An Archive is used to store serialized data. It can be used to save data to a file or load data from
        /// a file.
        /// @details An Archive holds a global context and the current state. The global context stores major data
        /// during serialization and deserialization, while the current state tracks the position in the data for
        /// reading or writing.
        class Archive {
        public:
            Archive() = default;
            virtual ~Archive() = default;
            Archive(const Archive &, Json *cursor = nullptr);

            Archive &operator=(const Archive &) = delete;

            struct GlobalContext {
                // The json object stores the data in a json format.
                Json json{};
                // The extra data is used to store additional data. Usually binary data.
                std::vector<BinData> extra_data{};
                // The resolvers are used to resolve pointers or handles or other user defined types.
                std::vector<std::unique_ptr<Resolver>> resolvers{};

                bool save_prepared = false;
                bool load_prepared = false;
            };

            /// @brief Prepare the archive for saving data. It will initialize the global context and set the cursor.
            void prepare_save();
            /// @brief Prepare the archive for loading data. It will initialize the global context and set the cursor.
            void prepare_load();
            /// @brief Clear the archive and reset it to the initial state.
            void clear();
            /**
             * @brief Create a new extra data buffer. Usually used in custom serialization function.
             * Make sure to set "%extra_data_id" in the currently serializing object's json to the returned index.
             *
             * @param extension suggested file extension for the extra data buffer (for example: ".fbx", ".png", etc.)
             * @return the index of the created extra data buffer
             */
            size_t create_new_extra_data_buffer(const std::string &extension = "");

            template <ResolverType T>
            T &GetOrCreateResolver() {
                for (auto &resolver : m_context->resolvers) {
                    if (dynamic_cast<T *>(resolver.get())) {
                        return *dynamic_cast<T *>(resolver.get());
                    }
                }
                m_context->resolvers.push_back(std::make_unique<T>());
                return *dynamic_cast<T *>(m_context->resolvers.back().get());
            }

            /// @brief Save the archive to a file.
            void save_to_file(std::filesystem::path path);
            /// @brief Load the archive from a file.
            void load_from_file(std::filesystem::path path);

            /// @brief Get the json property of the main object stored in the archive.
            /// @param property the property name
            /// @return the json object
            template <typename T>
            const Json &GetMainDataProperty(const T &property) const {
                auto &json = m_context->json;
                assert(json.contains("%main_data"));
                assert(json["%main_data"].contains(property));
                return json["%main_data"][property];
            }

        public:
            // The global context is shared between all archives.
            std::shared_ptr<GlobalContext> m_context = std::make_shared<GlobalContext>();
            // The current cursor is used to track the current position during serialization and deserialization.
            Json *m_cursor = nullptr;
        };
    } // namespace Serialization
} // namespace Engine

#endif // REFLECTION_ARCHIVE_INCLUDED
