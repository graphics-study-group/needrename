#include "ImmutableTexture.h"
#include <cassert>

namespace Engine
{
    ImmutableTexture::~ImmutableTexture()
    {
        this->Release();
    }

    bool ImmutableTexture::IsNativeValid() const noexcept
    {
        return glIsTexture(m_handle);
    }

    void ImmutableTexture::Release() noexcept
    {
        if (this->IsNativeValid()) {
            glDeleteTextures(1, &m_handle);
        }
        this->m_handle = 0;
    }
    
    void ImmutableTexture::Load()
    {
        Asset::Load();
    }

    void ImmutableTexture::Unload()
    {
        Asset::Unload();
    }

    void ImmutableTexture::BindToLocation(GLuint location) const
    {
        assert(location <= GL_MAX_TEXTURE_IMAGE_UNITS && "Texture location limit exceeded.");
        glActiveTexture(GL_TEXTURE0 + location);
        this->Bind();
    }

    std::tuple<GLsizei, GLsizei, GLsizei> ImmutableTexture::GetDimension() const noexcept
    {
        assert(this->IsValid());
        return this->dimensions;
    }

} // namespace Engine

