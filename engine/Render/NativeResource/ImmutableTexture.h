#ifndef RENDER_NATIVERESOURCE_IMMUTABLETEXTURE_INCLUDED
#define RENDER_NATIVERESOURCE_IMMUTABLETEXTURE_INCLUDED

#include "NativeResource.h"
#include "Asset/Asset.h"
#include <tuple>

namespace Engine
{
    /// @brief An immutable texture on GPU.
    /// Confer https://www.khronos.org/opengl/wiki/Texture_Storage for details
    class ImmutableTexture : public NativeResource, public Asset
    {
    public:
        virtual ~ImmutableTexture();
        bool IsNativeValid() const noexcept override;
        void Release() noexcept override;
        void Load() override;
        void Unload() override;

        /// @brief Create an immutable texture of given mipmap levels, format and dimensions, as if the texture name is generated, bound and allocated.
        /// All parameters are passed as-is to glTexStorage*.
        /// @param width width of the texture
        /// @param height depth of the texture, should be 0 for 1D textures
        /// @param depth depth of the texture, should be 0 for 1D and 2D textures
        /// @param format internal format of texels with size specification (e.g. GL_RGBA8)
        /// @param levels maximum level of mipmaps
        /// @return True if the creation is successful.
        /// @note You might want to further configure the texture with glTexParameter* after creation.
        virtual bool Create(GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei levels) = 0;

        /// @brief Upload data to the base image.
        /// Equivalent to calling glTexSubImage* with offsets 0, dimensions equal to the ones of the texture, and level 0.
        /// @param format format of the pixel data.
        /// @param type data type of the pixel data.
        /// @param data pointer to the data. Beware of memory access.
        /// @note You might want to adjust pixel storage parameters first with glPixelStore*().
        virtual bool FullUpload(GLenum format, GLenum type, GLvoid *data) const = 0;

        /// @brief Download data from texture on GPU
        /// @param data pointer to sufficiently large data buffer, allocated by the caller.
        virtual void Download(GLvoid *data) const = 0;

        /// @brief Bind this texture to location for shader.
        /// Equivalent to calling glBindTexture and then glActiveTexture.
        /// Use GetUnifrom from ShaderPass and glUniformi to set sampler uniform,
        /// and then invoke this member to bind texture.
        /// @param location location to be bound.
        void BindToLocation(GLuint location) const;

        /// @brief Bind this texture with glBindTexture.
        virtual void Bind() const = 0;

        std::tuple<GLsizei, GLsizei, GLsizei> GetDimension() const noexcept;

    protected:
        std::tuple<GLsizei, GLsizei, GLsizei> dimensions = {0, 0, 0};
    };
} // namespace Engine

#endif // RENDER_NATIVERESOURCE_IMMUTABLETEXTURE_INCLUDED
