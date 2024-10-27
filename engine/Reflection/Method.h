#ifndef REFLECTION_METHOD_INCLUDED
#define REFLECTION_METHOD_INCLUDED

#include <string>
#include <functional>
#include <vector>
#include <memory>
#include "utils.h"

namespace Engine
{
    namespace Reflection
    {
        class Type;
        class Var;

        class Method
        {
        protected:
            friend class Type;
            Method() = delete;
            Method(const std::string &name, const WrapperMemberFunc &func, std::shared_ptr<Type> return_type);
            template <typename T>
            Method(const std::string &name, const WrapperMemberFunc &func, std::shared_ptr<Type> return_type, T original_func);
        public:
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
