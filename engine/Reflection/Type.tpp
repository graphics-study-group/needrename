#include "Field.h"
#include "Method.h"
#include "Type.h"
#include "Var.h"
#include "utils.h"

namespace Engine {
    namespace Reflection {
        template <typename... Args>
        void Type::AddConstructor(const WrapperMemberFunc &func) {
            std::string mangled_name = k_constructor_name + GetMangledName<Args...>();
            m_methods[mangled_name] =
                std::shared_ptr<const Method>(new Method(mangled_name, func, shared_from_this(), true));
        }

        template <typename... Args>
        void Type::AddMethod(
            const std::string &name,
            const WrapperMemberFunc &func,
            std::shared_ptr<const Type> return_type,
            bool is_const
        ) {
            std::string mangled_name = name + GetMangledName<Args...>();
            m_methods[mangled_name] =
                std::shared_ptr<const Method>(new Method(mangled_name, func, return_type, is_const, true));
        }

        template <typename... Args>
        Var Type::CreateInstance(Args &&...args) const {
            auto constructor = GetMethod(k_constructor_name, std::forward<Args>(args)...);
            if (!constructor) throw std::runtime_error("Constructor not found");
            return constructor->Invoke(nullptr, std::forward<Args>(args)...);
        }

        template <typename... Args>
        std::shared_ptr<const Method> Type::GetMethod(const std::string &name, Args &&...args) const {
            auto mangled_name = name + GetMangledName(std::forward<Args>(args)...);
            return GetMethodFromMangledName(mangled_name);
        }
    } // namespace Reflection
} // namespace Engine
