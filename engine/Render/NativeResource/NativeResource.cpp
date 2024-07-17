#include "NativeResource.h"
#include <cassert>

namespace Engine {
    NativeResource::~NativeResource()
    {
        assert(m_handle == 0 && "A native resource is destructed without releasing.");
    }

    NativeResource::NativeResource(NativeResource && other)
    {
        // Set m_handle to avoid spurious destruction.
        other.m_handle = 0;
    }

    NativeResource & NativeResource::operator=(NativeResource && other)
    {
        assert(&other != this && "Self move assignment detected.");

        // Steal the handle from other.
        this->Release();
        this->m_handle = other.m_handle;

        // Set m_handle to avoid spurious destruction.
        other.m_handle = 0;
        return *this;
    }

    GLuint NativeResource::GetHandle() const noexcept
    {
        return m_handle;
    }
}
