#include "Method.h"

namespace Engine
{
    namespace Reflection
    {
        Method::Method(const std::string &final_name, WrapperMemberFunc func, std::shared_ptr<Type> return_type)
            : m_name(final_name), m_func(func), m_return_type(return_type)
        {
        }
    }
}
