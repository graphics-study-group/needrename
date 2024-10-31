#include "Method.h"
#include "Var.h"
#include "utils.h"

namespace Engine
{
    namespace Reflection
    {
        template <typename... Args>
        Method::Method(const std::string &name_no_mangled, const WrapperMemberFunc &func, const WrapperConstMemberFunc &const_func, std::shared_ptr<Type> return_type)
            : m_name(name_no_mangled), m_func(func), m_const_func(const_func), m_return_type(return_type)
        {
            m_name += GetMangledName<Args...>();
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

        template <typename... Args>
        Var Method::ConstInvoke(ConstVar &obj, Args... args)
        {
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(reinterpret_cast<void *>(std::addressof(args))), ...);
            void *ret = nullptr;
            m_const_func(obj.GetDataPtr(), ret, arg_pointers);
            return Var(m_return_type, ret);
        }

        template <typename... Args>
        Var Method::ConstInvoke(const void *obj, Args... args)
        {
            std::vector<void *> arg_pointers;
            (arg_pointers.push_back(reinterpret_cast<void *>(std::addressof(args))), ...);
            void *ret = nullptr;
            m_const_func(obj, ret, arg_pointers);
            return Var(m_return_type, ret);
        }
    }
}
