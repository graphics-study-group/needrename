#include "Method.h"
#include "Var.h"
#include "utils.h"

namespace Engine
{
    namespace Reflection
    {
        template <typename T>
        Method::Method(const std::string &name_no_mangled, WrapperMemberFunc func, std::shared_ptr<Type> return_type, T original_func)
            : m_name(name_no_mangled), m_func(func), m_return_type(return_type)
        {
            static_assert(std::is_member_function_pointer_v<T>);
            m_name += GetFunctionArgsMangledName(original_func);
        }

        template <typename... Args>
        Var Method::Invoke(Var &obj, Args... args)
        {
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(reinterpret_cast<void *>(std::addressof(args))), ...);
            void *ret = nullptr;
            m_func(obj.GetDataPtr(), ret, arg_pointers);
            return Var(m_return_type, ret);
        }

        template <typename... Args>
        Var Method::Invoke(void *obj, Args... args)
        {
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(reinterpret_cast<void *>(std::addressof(args))), ...);
            void *ret = nullptr;
            m_func(obj, ret, arg_pointers);
            return Var(m_return_type, ret);
        }
    }
}
