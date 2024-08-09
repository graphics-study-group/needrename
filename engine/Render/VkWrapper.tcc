#ifndef RENDER_VKWRAPPER_INCLUDED
#define RENDER_VKWRAPPER_INCLUDED

#include <concepts>
#include <utility>

#include "Render/RenderSystem.h"

namespace Engine
{
    template <class T>
    concept IsVulkanHandleWrapper = requires {
        T::objectType;
    };

    template <class T>
    concept IsUniqueVulkanHandleWrapper = IsVulkanHandleWrapper<typename T::element_type> && requires (T handle) {
        requires std::same_as<typename T::element_type, std::remove_reference_t<decltype(handle.get())>>;
    };

    /// @brief A move-only wrapper for vk namespace wrappers
    template <class T> requires IsUniqueVulkanHandleWrapper <T>
    class VkWrapper 
    {
    public:
        typedef T::element_type element_type;
        VkWrapper (std::weak_ptr <RenderSystem> system) : m_system(system) {};
        virtual ~VkWrapper () = default;

        VkWrapper(const VkWrapper &) = delete;
        VkWrapper(VkWrapper && other) : m_handle(std::move(other.handle)) {};
        VkWrapper & operator= (const VkWrapper &) = delete;
        VkWrapper & operator= (VkWrapper & other) {
            if (this->handle != other.handle) {
                this->Release();
                std::swap(this->handle, other.handle);
            }
        }

        /// @brief Release the resource wrapped.
        void release() { m_handle.release(); };

        element_type & get() { return m_handle.get(); };
        const element_type & get() const { return m_handle.get(); };
    
    protected:
        std::weak_ptr <RenderSystem> m_system {};
        T m_handle {};
    };
} // namespace Engine


#endif // RENDER_VKWRAPPER_INCLUDED
