#include "Type.h"
#include "Field.h"
#include "Var.h"
#include "Method.h"
#include "utils.h"

namespace Engine
{
    namespace Reflection
    {
        template <typename... Args>
        void Type::AddConstructor(const WrapperMemberFunc &func)
        {
            std::string mangled_name = k_constructor_name + GetMangledName<Args...>();
            m_methods[mangled_name] = std::shared_ptr<Method>(new Method(mangled_name, func, nullptr, shared_from_this(), true));
        }

        template <typename... Args>
        void Type::AddMethod(const std::string &name, const WrapperMemberFunc &func, const WrapperConstMemberFunc &const_func, std::shared_ptr<Type> return_type)
        {
            std::string mangled_name = name + GetMangledName<Args...>();
            m_methods[mangled_name] = std::shared_ptr<Method>(new Method(mangled_name, func, const_func, return_type, true));
        }

        template <typename... Args>
        Var Type::CreateInstance(Args&&... args)
        {
            auto constructor = GetMethod(k_constructor_name, std::forward<Args>(args)...);
            if (!constructor)
                throw std::runtime_error("Constructor not found");
            return constructor->Invoke(nullptr, std::forward<Args>(args)...);
        }

        template <typename... Args>
        std::shared_ptr<Method> Type::GetMethod(const std::string &name, Args&&... args)
        {
            auto mangled_name = name + GetMangledName(std::forward<Args>(args)...);
            return GetMethodFromManagedName(mangled_name);
        }
    }
}
