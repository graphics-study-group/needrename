#ifndef REFLECTION_UTILS_INCLUDED
#define REFLECTION_UTILS_INCLUDED

#include <functional>
#include <string>
#include <vector>

// use type of parameters only
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace Engine {
    namespace Reflection {
        /// @brief Wrapper function for member function. 
        /// Format: <void func(the owner object of the member function, return value, arguments)>
        using WrapperMemberFunc = std::function<void(void *, void *&, std::vector<void *>)>;
        /// @brief Wrapper function for getting a field of an object. 
        /// Format: <void func(the owner object of the field, return value)>
        using WrapperFieldFunc = std::function<void(void *, void *&)>;
        /// @brief Wrapper function for getting an element of an array field in an object.
        /// Format: <void func(the owner object of the array field, index, return value)>
        using WrapperArrayFieldFunc = std::function<void(void *, size_t, void *&)>;
        /// @brief Wrapper function for getting the size of an array field in an object.
        /// Format: <void func(the owner object of the array field, return value)>
        using WrapperArrayFieldSize = std::function<void(void *, size_t &)>;

        /// @brief Get the mangled name of some arguments.
        template <typename... Args>
        std::string GetMangledName() {
            std::string name = std::to_string(sizeof...(Args));
            ((name += typeid(Args).name()), ...);
            return name;
        }

        /// @brief Get the mangled name of some arguments.
        template <typename... Args>
        std::string GetMangledName(Args &&...args) {
            return GetMangledName<Args...>();
        }
    } // namespace Reflection

    namespace Serialization {
        /// @brief A marker struct used to identify the serialization process
        struct SerializationMarker {};
    } // namespace Serialization
} // namespace Engine

#pragma GCC diagnostic pop

#endif // REFLECTION_UTILS_INCLUDED
