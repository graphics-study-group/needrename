#include "ImmutableTexture.h"
#include <cassert>

namespace Engine
{
    ImmutableTexture::~ImmutableTexture()
    {
        this->Release();
    }

    bool ImmutableTexture::IsValid() const noexcept
    {
        return glIsTexture(m_handle);
    }

    void ImmutableTexture::Release() noexcept
    {
        if (this->IsValid()) {
            glDeleteTextures(1, &m_handle);
        }
        this->m_handle = 0;
    }

    std::tuple<GLsizei, GLsizei, GLsizei> ImmutableTexture::GetDimension() const noexcept
    {
        assert(this->IsValid());
        GLsizei w, h, d;
        
        return std::tuple<GLsizei, GLsizei, GLsizei>();
    }

} // namespace Engine

