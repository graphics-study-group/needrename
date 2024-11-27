#ifndef REFLECTION_SERIALIZATION_INCLUDED
#define REFLECTION_SERIALIZATION_INCLUDED

#include <vector>
#include "Archive.h"

namespace Engine
{
    namespace Serialization
    {
        template <typename T>
        void save_to_archive(const std::vector<T> &value, Archive &buffer);
        template <typename T>
        void load_from_archive(std::vector<T> &value, Archive &buffer);

        template <typename T>
        void save_to_archive(const std::shared_ptr<T> &value, Archive &buffer);
        template <typename T>
        void load_from_archive(std::shared_ptr<T> &value, Archive &buffer);

        template <typename T>
        void save_to_archive(const std::unique_ptr<T> &value, Archive &buffer);
        template <typename T>
        void load_from_archive(std::unique_ptr<T> &value, Archive &buffer);

        template <typename T>
        void save_to_archive(const std::weak_ptr<T> &value, Archive &buffer);
        template <typename T>
        void load_from_archive(std::weak_ptr<T> &value, Archive &buffer);

        template <typename T>
        class has_custom_save
        {
            template <typename U>
            static auto test(int) -> decltype(std::declval<U>().save_to_archive(std::declval<Archive &>()), std::true_type());

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_custom_load
        {
            template <typename U>
            static auto test(int) -> decltype(std::declval<U>().load_from_archive(std::declval<Archive &>()), std::true_type());

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_special_save
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (*)(const U &, Archive &)>(Engine::Serialization::save_to_archive), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_special_load
        {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (*)(U &, Archive &)>(Engine::Serialization::load_from_archive), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_generated_save
        {
            template <typename U>
            static auto test(int) -> decltype(std::declval<U>().__serialization_save__(std::declval<Archive &>()), std::true_type());

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_generated_load
        {
            template <typename U>
            static auto test(int) -> decltype(std::declval<U>().__serialization_load__(std::declval<Archive &>()), std::true_type());

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        typename std::enable_if<has_custom_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            value.save_to_archive(buffer);
        }

        template <typename T>
        typename std::enable_if<has_custom_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            value.load_from_archive(buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && has_special_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            Engine::Serialization::save_to_archive(value, buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_load<T>::value && has_special_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            Engine::Serialization::load_from_archive(value, buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && !has_special_save<T>::value && has_generated_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            value.__serialization_save__(buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_load<T>::value && !has_special_load<T>::value && has_generated_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            value.__serialization_load__(buffer);
        }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && !has_generated_save<T>::value && !has_special_save<T>::value, void>::type
        serialize(const T &value, Archive &buffer)
        {
            throw std::runtime_error(std::string("No serialization function found for type: ") + typeid(T).name() + "\nNote: \n"
                                     +"  - If the type is a custom type, make sure to define a member function like save_to_archive(Engine::Serialization::Archive &)\n"
                                     +"  - The type must have a default constructor with no arguments\n"
                                     +"  - Do not use pointers, use std::shared_ptr or std::unique_ptr instead\n");
        }

        template <typename T>
        typename std::enable_if<!has_custom_load<T>::value && !has_generated_load<T>::value && !has_special_load<T>::value, void>::type
        deserialize(T &value, Archive &buffer)
        {
            throw std::runtime_error(std::string("No deserialization function found for type: ") + typeid(T).name() + "\nNote: \n"
                                     +"  - If the type is a custom type, make sure to define a member function like load_from_archive(Engine::Serialization::Archive &)\n"
                                     +"  - The type must have a default constructor with no arguments\n"
                                     +"  - Do not use pointers, use std::shared_ptr or std::unique_ptr instead\n");
        }

#pragma GCC diagnostic pop

    }
}

#include "serialization.tpp"

#endif // REFLECTION_SERIALIZATION_INCLUDED
