#include "NativeResource.h"

#ifndef NDEBUG
#include <iostream>
#endif

namespace Engine {
    NativeResource::~NativeResource()
    {
#ifndef NDEBUG
        std::cerr << "GPU native resource with handle " << m_handle << " destructed." << std::endl;
#endif
    }

    GLuint NativeResource::GetHandle() const noexcept
    {
        return m_handle;
    }
}
