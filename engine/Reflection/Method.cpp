#include "Method.h"

namespace Engine {
    namespace Reflection {
        Method::Method(
            const std::string &final_name,
            const WrapperMemberFunc &func,
            std::shared_ptr<const Type> return_type,
            bool is_const,
            bool is_final_name
        ) : m_func(func), m_return_type(return_type), m_is_const(is_const), m_name(final_name) {
            assert(is_final_name == true);
        }
    } // namespace Reflection
} // namespace Engine
