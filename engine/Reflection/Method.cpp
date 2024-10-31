#include "Method.h"

namespace Engine
{
    namespace Reflection
    {
        Method::Method(const std::string &final_name, const WrapperMemberFunc &func, const WrapperConstMemberFunc &const_func, std::shared_ptr<Type> return_type, bool is_final_name)
            : m_func(func), m_const_func(const_func), m_return_type(return_type), m_name(final_name)
        {
            assert(is_final_name == true);
        }
    }
}
