#ifndef RENDER_NATIVERESOURCE_NATIVERESOURCE_INCLUDED
#define RENDER_NATIVERESOURCE_NATIVERESOURCE_INCLUDED

#include <glad/glad.h>

namespace Engine {
    /// @brief An abstract base class for native resources on GPU
    class NativeResource {
    public:
        NativeResource() = default;
        virtual ~NativeResource();

        // Disable copy constructor
        NativeResource(const NativeResource &) = delete;
        // Disable copy operator
        const NativeResource & operator= (const NativeResource &) = delete;

        /// @brief Get the native handle (of type GLuint) of the object;
        /// @return the native handle
        GLuint GetHandle () const noexcept;

        /// @brief Release the object represented by this class if it exists.
        virtual void Release() noexcept = 0;

        /// @brief Check if the object holds an actual object on GPU.
        /// @return True if valid.
        virtual bool IsValid() const noexcept = 0;
    protected:
        GLuint m_handle = 0;
    };
}

#endif // RENDER_NATIVERESOURCE_NATIVERESOURCE_INCLUDED
