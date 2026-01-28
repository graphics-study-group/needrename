#include "Handle.h"

namespace Engine {
    namespace detail {
        HandleBase::HandleBase(uint32_t data) : m_data(data) {
        }

        bool HandleBase::IsValid() const noexcept {
            return m_data != 0u;
        }

        uint32_t HandleBase::GetData() const noexcept {
            return m_data;
        }

        bool HandleBase::operator==(const HandleBase &other) const noexcept {
            return m_data == other.m_data;
        }
    } // namespace detail

    ObjectHandle::ObjectHandle(uint32_t data) : HandleBase(data) {
    }

    ComponentHandle::ComponentHandle(uint32_t data) : HandleBase(data) {
    }
} // namespace Engine
