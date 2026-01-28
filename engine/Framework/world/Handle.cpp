#include "Handle.h"

namespace Engine {
    namespace detail {
        HandleBase::HandleBase(GUID data) : m_data(data) {
        }

        bool HandleBase::IsValid() const noexcept {
            return m_data != GUID(); // not zero
        }

        void HandleBase::Reset() noexcept {
            m_data = GUID(); // set zero
        }

        GUID HandleBase::GetData() const noexcept {
            return m_data;
        }

        bool HandleBase::operator==(const HandleBase &other) const noexcept {
            return m_data == other.m_data;
        }

        void HandleBase::save_to_archive(Serialization::Archive &archive) const {
            m_data.save_to_archive(archive);
        }

        void HandleBase::load_from_archive(Serialization::Archive &archive) {
            m_data.load_from_archive(archive);
        }
    } // namespace detail

    ObjectHandle::ObjectHandle(GUID data) : HandleBase(data) {
    }

    ComponentHandle::ComponentHandle(GUID data) : HandleBase(data) {
    }
} // namespace Engine
