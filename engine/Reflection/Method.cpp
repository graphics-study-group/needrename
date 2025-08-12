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

        std::shared_ptr<const Type> Method::GetReturnType() const {
            return m_return_type;
        }

        bool Method::IsConst() const {
            return m_is_const;
        }

        const std::string &Method::GetName() const {
            return m_name;
        }
    } // namespace Reflection
} // namespace Engine
