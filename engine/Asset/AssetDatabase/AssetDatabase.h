#ifndef ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
#define ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED

#include <Core/guid.h>
#include <filesystem>
#include <iterator>
#include <memory>

namespace Engine {
    namespace Serialization {
        class Archive;
    }

    /**
     * @brief An interface defines a map between asset paths and their data.
     * @details This interface provides methods to save and load archives to and from the database.
     * Different implementations can use different storage backends, such as file system,
     * or packaged files.
     */
    class AssetDatabase {
    public:
        AssetDatabase() = default;
        virtual ~AssetDatabase() = default;

        using AssetPair = std::pair<std::filesystem::path, GUID>;

        /**
         * @brief A lightweight input-range that can be used in range-based for.
         * This range store a IterImplBase pointer to implement the iteration.
         * The IterImplBase class should be implemented by the derived classes to provide
        */
        class ListRange {
        public:
            /**
             * @brief The base class for the iterator implementation. 
             * The iterator should store the begin/end state.
             * The iterator should implement the next() method to produce the next element.
             * The begin_clone() method should return a new iterator at the begin state.
             */
            struct IterImplBase {
                virtual bool next(AssetPair &out) = 0; // produce next element; return false on end
                virtual std::unique_ptr<IterImplBase> begin_clone() const = 0;
                virtual ~IterImplBase() = default;
            };

            class iterator {
            public:
                using value_type = AssetPair;
                using difference_type = std::ptrdiff_t;
                using iterator_category = std::input_iterator_tag;
                using reference = const value_type &;
                using pointer = const value_type *;

                iterator() = default;
                explicit iterator(std::unique_ptr<IterImplBase> impl) : m_impl(std::move(impl)) {
                    ++(*this);
                }

                reference operator*() const {
                    return m_current;
                }
                pointer operator->() const {
                    return &m_current;
                }

                iterator &operator++() {
                    if (m_impl) {
                        AssetPair temp{};
                        if (m_impl->next(temp)) {
                            m_current = std::move(temp);
                        } else {
                            m_impl.reset();
                        }
                    }
                    return *this;
                }

                bool operator==(const iterator &other) const {
                    return !m_impl && !other.m_impl;
                }
                bool operator!=(const iterator &other) const {
                    return !(*this == other);
                }

            private:
                std::unique_ptr<IterImplBase> m_impl{};
                value_type m_current{};
            };

            ListRange() = default;
            explicit ListRange(std::unique_ptr<IterImplBase> impl) : m_impl(std::move(impl)) {
            }

            iterator begin() const {
                return iterator(m_impl ? m_impl->begin_clone() : nullptr);
            }
            iterator end() const {
                return iterator();
            }

        private:
            std::unique_ptr<IterImplBase> m_impl{};
        };

        /// @brief Save the archive.
        virtual void SaveArchive(Serialization::Archive &archive, std::filesystem::path path) = 0;
        /// @brief Load the archive.
        virtual void LoadArchive(Serialization::Archive &archive, std::filesystem::path path) = 0;
        /**
         * @brief List all assets in the specified directory.
         *
         * @param directory The directory to search for assets.
         * @param recursive Whether to search recursively.
         * @return A lazy input-range of (project_path, GUID) pairs suitable for range-based for.
         */
        virtual ListRange ListAssets(std::filesystem::path directory = {}, bool recursive = true) = 0;
    };
} // namespace Engine

#endif // ASSET_ASSETDATABASE_ASSETDATABASE_INCLUDED
