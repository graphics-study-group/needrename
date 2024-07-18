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

    void ImmutableTexture::BindToLocation(GLuint location) const
    {
        assert(location <= GL_MAX_TEXTURE_IMAGE_UNITS && "Texture location limit exceeded.");
        this->Bind();
        glActiveTexture(GL_TEXTURE0 + location);
    }

    std::tuple<GLsizei, GLsizei, GLsizei> ImmutableTexture::GetDimension() const noexcept
    {
        assert(this->IsValid());
        return this->dimensions;
    }

} // namespace Engine

