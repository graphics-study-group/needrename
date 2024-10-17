#include "Method.h"
#include "Var.h"

namespace Engine
{
    namespace Reflection
    {
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
