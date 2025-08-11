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

        class Method {
        protected:
            friend class Type;
            Method() = delete;
            Method(
                const std::string &final_name,
                const WrapperMemberFunc &func,
                std::shared_ptr<const Type> return_type,
                bool is_const,
                bool is_final_name
            );
            template <typename... Args>
            Method(
                const std::string &name,
                const WrapperMemberFunc &func,
                std::shared_ptr<const Type> return_type,
                bool is_const
            );

        public:
            ~Method() = default;

        protected:
            WrapperMemberFunc m_func{};

        public:
            std::shared_ptr<const Type> m_return_type{};
            bool m_is_const = false;
            std::string m_name{};

            template <typename... Args>
            Var Invoke(Var &obj, Args &&...) const;
            template <typename... Args>
            Var Invoke(void *obj, Args &&...) const;
        };
    } // namespace Reflection
} // namespace Engine

#include "Method.tpp"

#endif // REFLECTION_METHOD_INCLUDED
