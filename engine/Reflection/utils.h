#ifndef REFLECTION_UTILS_INCLUDED
#define REFLECTION_UTILS_INCLUDED

#include <string>
#include <vector>
#include <functional>

namespace Engine
{
    namespace Reflection
    {
        /// @brief Wrapper function for member function. Format: <void func(the owerner object of the member function, return value, arguments)>
        using WrapperMemberFunc = std::function<void(void *, void *&, std::vector<void *>)>;

        template <typename... Args>
        std::string GetMangledName()
        {
            std::string name = std::to_string(sizeof...(Args));
            ((name += typeid(Args).name()), ...);
            return name;
        }

        template <typename... Args>
        std::string GetMangledName(Args... args)
        {
            return GetMangledName<Args...>();
        }

        template <typename T>
        struct FunctionTraits;

        template <typename R, typename... Args>
        struct FunctionTraits<R (*)(Args...)>
        {
            using ReturnType = R;
            using ParameterTypes = std::tuple<Args...>;
        };

        template <typename R, typename cls, typename... Args>
        struct FunctionTraits<R (cls::*)(Args...)>
        {
            using ReturnType = R;
            using ParameterTypes = std::tuple<Args...>;
        };

        template <typename T>
        std::string GetFunctionArgsMangledName(T func)
        {
            using Traits = FunctionTraits<T>;
            using ParameterTypes = typename Traits::ParameterTypes;
            std::string ret = std::to_string(std::tuple_size<ParameterTypes>::value);
            std::apply([&ret](auto &&...args)
                       { ((ret += typeid(args).name()), ...); }, ParameterTypes{});
            return ret;
        }
    }
}

#endif // REFLECTION_UTILS_INCLUDED
