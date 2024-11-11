#ifndef REFLECTION_SERIALIZATION_INCLUDED
#define REFLECTION_SERIALIZATION_INCLUDED

#include <vector>
#include <nlohmann/json.hpp>

namespace Engine
{
    namespace Serialization
    {
        using Json = nlohmann::json;
        struct Archive
        {
            Json json{};
            std::vector<std::pair<std::string, std::shared_ptr<std::byte>>> buffers{}; // Identification string, buffer start pointer

            void clear()
            {
                json.clear();
                buffers.clear();
            }
        };

        template <typename T>
        void save(const std::vector<T> &value, Archive &buffer);
        template <typename T>
        void load(std::vector<T> &value, Archive &buffer);

        template <typename T>
        void save(const std::shared_ptr<T> &value, Archive &buffer);
        template <typename T>
        void load(std::shared_ptr<T> &value, Archive &buffer);

        template <typename T>
        void save(const std::unique_ptr<T> &value, Archive &buffer);
        template <typename T>
        void load(std::unique_ptr<T> &value, Archive &buffer);

        template <typename T>
        void save(const std::weak_ptr<T> &value, Archive &buffer);
        template <typename T>
        void load(std::weak_ptr<T> &value, Archive &buffer);

        template <typename T>
        class has_custom_save
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (U::*)(Archive &) const>(&U::save), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_custom_load
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (U::*)(Archive &)>(&U::load), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_generated_save
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (U::*)(Archive &) const>(&U::__serialization_save__), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_generated_load
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (U::*)(Archive &)>(&U::__serialization_load__), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_special_save
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (*)(const U &, Archive &)>(Engine::Serialization::save), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_special_load
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (*)(U &, Archive &)>(Engine::Serialization::load), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        typename std::enable_if<has_custom_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            value.save(buffer);
        }

        template <typename T>
        typename std::enable_if<has_custom_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            value.load(buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && has_generated_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            value.__serialization_save__(buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_load<T>::value && has_generated_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            value.__serialization_load__(buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && !has_generated_save<T>::value && has_special_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            Engine::Serialization::save(value, buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_load<T>::value && !has_generated_load<T>::value && has_special_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            Engine::Serialization::load(value, buffer);
        }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && !has_generated_save<T>::value && !has_special_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            throw std::runtime_error("No serialization function found for type");
        }

        template <typename T>
        typename std::enable_if<!has_custom_load<T>::value && !has_generated_load<T>::value && !has_special_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            throw std::runtime_error("No deserialization function found for type");
        }

#pragma GCC diagnostic pop

    }
}

#include "serialization.tpp"

#endif // REFLECTION_SERIALIZATION_INCLUDED
