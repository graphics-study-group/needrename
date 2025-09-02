#include "Method.h"

namespace Engine {
    namespace Reflection {
        Method::Method(
            const std::string &final_name,
            const WrapperMemberFunc &func,
            std::shared_ptr<const Type> return_type,
            bool is_const,
            bool return_value_needs_free
        ) :
            m_func(func), m_return_type(return_type), m_is_const(is_const),
            m_return_value_needs_free(return_value_needs_free), m_name(final_name) {
        }

        std::shared_ptr<const Type> Method::GetReturnType() const {
            return m_return_type;
        }

        bool Method::IsConst() const {
            return m_is_const;
        }

        bool Method::ReturnValueNeedsFree() const {
            return m_return_value_needs_free;
        }

        const std::string &Method::GetName() const {
            return m_name;
        }
    } // namespace Reflection
} // namespace Engine
