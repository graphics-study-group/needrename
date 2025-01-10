#ifndef REFLECTION_UTILS_INCLUDED
#define REFLECTION_UTILS_INCLUDED

#include <string>
#include <vector>
#include <functional>

// use type of parameters only
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

namespace Engine
{
    namespace Reflection
    {
        /// @brief Wrapper function for member function. Format: <void func(the owner object of the member function, return value, arguments)>
        using WrapperMemberFunc = std::function<void(void *, void *&, std::vector<void *>)>;
        /// @brief Wrapper function for const member function. Format: <void func(the owner object of the member function, return value, arguments)>
        using WrapperConstMemberFunc = std::function<void(const void *, void *&, std::vector<void *>)>;
        /// @brief Wrapper function for getting a field of an object. Format: <void func(the owner object of the field, return value)>
        using WrapperFieldFunc = std::function<void(void *, void *&)>;
        /// @brief Wrapper function for getting a const field of an object. Format: <void func(the owner object of the field, return value)>
        using WrapperConstFieldFunc = std::function<void(const void *, const void *&)>;

        /// @brief Get the mangled name of some arguments.
        template <typename... Args>
        std::string GetMangledName()
        {
            std::string name = std::to_string(sizeof...(Args));
            ((name += typeid(Args).name()), ...);
            return name;
        }

        /// @brief Get the mangled name of some arguments.
        template <typename... Args>
        std::string GetMangledName(Args&&... args)
        {
            return GetMangledName<Args...>();
        }
    }
}

#pragma GCC diagnostic pop

#endif // REFLECTION_UTILS_INCLUDED
