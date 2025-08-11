#ifndef REFLECTION_METHOD_INCLUDED
#define REFLECTION_METHOD_INCLUDED

#include "utils.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Engine {
    namespace Reflection {
        class Type;
        class Var;
        class ConstVar;

        class Method {
        protected:
            friend class Type;
            Method() = delete;
            Method(
                const std::string &final_name,
                const WrapperMemberFunc &func,
                const WrapperConstMemberFunc &const_func,
                std::shared_ptr<Type> return_type,
                bool is_final_name
            );
            template <typename... Args>
            Method(
                const std::string &name,
                const WrapperMemberFunc &func,
                const WrapperConstMemberFunc &const_func,
                std::shared_ptr<Type> return_type
            );

        public:
            ~Method() = default;

        protected:
            WrapperMemberFunc m_func{};
            WrapperConstMemberFunc m_const_func{};

        public:
            std::shared_ptr<Type> m_return_type{};
            std::string m_name{};

            template <typename... Args>
            Var Invoke(Var &obj, Args &&...);
            template <typename... Args>
            Var Invoke(void *obj, Args &&...);
            template <typename... Args>
            Var ConstInvoke(ConstVar &obj, Args &&...);
            template <typename... Args>
            Var ConstInvoke(const void *obj, Args &&...);
        };
    } // namespace Reflection
} // namespace Engine

#include "Method.tpp"

#endif // REFLECTION_METHOD_INCLUDED
