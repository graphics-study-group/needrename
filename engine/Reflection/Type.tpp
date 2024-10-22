#include "Type.h"
#include "Field.h"
#include "Var.h"
#include "Method.h"
#include "utils.h"

namespace Engine
{
    namespace Reflection
    {
        template <typename T>
        void Type::AddMethod(const std::string &name, WrapperMemberFunc func, std::shared_ptr<Type> return_type, T original_func)
        {
            static_assert(std::is_member_function_pointer_v<T>);
            std::string mangled_name = name + GetFunctionArgsMangledName(original_func);
            m_methods[mangled_name] = std::make_shared<Method>(mangled_name, func, return_type);
        }

        template <typename T>
        void Type::AddField(const std::shared_ptr<Type> field_type, const std::string &name, T field)
        {
            static_assert(std::is_member_pointer_v<T>);
            m_fields[name] = std::make_shared<Field>(shared_from_this(), field_type, *reinterpret_cast<std::uintptr_t *>(&field));
        }

        template <typename... Args>
        Var Type::CreateInstance(Args... args)
        {
            auto constructor = GetMethod(constructer_name, args...);
            if (!constructor)
                throw std::runtime_error("Constructor not found");
            return constructor->Invoke(nullptr, args...);
        }

        template <typename... Args>
        std::shared_ptr<Method> Type::GetMethod(const std::string &name, Args... args)
        {
            auto mangled_name = name + GetMangledName(args...);
            return GetMethodFromManagedName(mangled_name);
        }
    }
}
