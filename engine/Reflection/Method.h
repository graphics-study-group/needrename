#ifndef REFLECTION_METHOD_INCLUDED
#define REFLECTION_METHOD_INCLUDED

#include <string>
#include <functional>
#include <vector>
#include <memory>

namespace Engine
{
    namespace Reflection
    {
        class Type;
        class Var;

        /// @brief Wrapper function for member function. Format: <the owerner object of the member function, return value, arguments>
        using WrapperMemberFunc = std::function<void(void *, void *&, std::vector<void *>)>;

        class Method
        {
        public:
            Method() = delete;
            Method(const std::string &name, WrapperMemberFunc func, std::shared_ptr<Type> return_type);
            ~Method() = default;

        protected:
            WrapperMemberFunc m_func{};

        public:
            std::shared_ptr<Type> m_return_type{};
            std::string m_name{};

            template <typename... Args>
            Var Invoke(Var &obj, Args...);
            template <typename... Args>
            Var Invoke(void *obj, Args...);
        };
    }
}

#include "Method.tpp"

#endif // REFLECTION_METHOD_INCLUDED
