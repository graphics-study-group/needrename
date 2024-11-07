#ifndef REFLECTION_SERIALIZATION_INCLUDED
#define REFLECTION_SERIALIZATION_INCLUDED

#include <vector>
#include <nlohmann/json.hpp>

#include "generated/generated_serialization.hpp"

namespace Engine
{
    namespace Serialization
    {
        using Json = nlohmann::json;
        struct Archive
        {
            Json json {};
            std::vector<std::pair<std::string, std::shared_ptr<std::byte>>> buffers {}; // Identification string, buffer start pointer

            void clear()
            {
                json.clear();
                buffers.clear();
            }
        };

        template <typename T>
        class has_custom_save {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (U::*)(Archive&) const>(&U::save), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        class has_generated_save {
            template <typename U>
            static auto test(int) -> decltype((static_cast<void (*)(const U&, Archive&)>(Engine::Serialization::save), std::true_type()));

            template <typename>
            static std::false_type test(...);

        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

        template <typename T>
        typename std::enable_if<has_custom_save<T>::value && has_generated_save<T>::value, void>::type
        serialize(const T& value, Archive& buffer)
        {
            value.save(buffer);
        }

        template <typename T>
        typename std::enable_if<has_custom_save<T>::value && !has_generated_save<T>::value, void>::type
        serialize(const T& value, Archive& buffer)
        {
            value.save(buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && has_generated_save<T>::value, void>::type
        serialize(const T& value, Archive& buffer)
        {
            Engine::Serialization::save(value, buffer);
        }

        template <typename T>
        typename std::enable_if<!has_custom_save<T>::value && !has_generated_save<T>::value, void>::type
        serialize(const T& value, Archive& buffer)
        {
            throw std::runtime_error("No serialization function found for type");
        }
    }
}

#endif
