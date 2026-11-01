#ifndef ENGINE_RHI_SHADER_MODULE_INCLUDED
#define ENGINE_RHI_SHADER_MODULE_INCLUDED

#include "Rhi/RhiTypes.h"
#include "Rhi/rhi_export.h"

#include <memory>
#include <span>
#include <string_view>

namespace Engine::Rhi {

    class DeviceInterface;

    /**
     * @brief Compiled shader code the device can build a pipeline from.
     *
     * A module is one shader's code, already translated into whatever the backend
     * consumes. It has no stage: the same module can be used by a stage the caller
     * names when it builds a pipeline, and the two are separate facts.
     *
     * @note Movable but non-copyable.
     */
    class RHI_API ShaderModule {
    protected:
        struct impl;
        std::unique_ptr<impl> pimpl;

        ShaderModule(std::unique_ptr<impl> p) noexcept;

    public:
        /**
         * @brief Create a module from the backend's own code format.
         *
         * Vulkan takes SPIR-V; a backend that takes WGSL would translate it here,
         * which is why the bytes are named as "the backend's code" rather than as
         * SPIR-V.
         *
         * @param device_interface The device that owns it.
         * @param code The compiled code.
         * @param name A debug name, shown by validation and capture tools.
         */
        static ShaderModule Create(
            const DeviceInterface &device_interface, std::span<const std::byte> code, std::string_view name = {}
        );

        ~ShaderModule();

        ShaderModule(const ShaderModule &) = delete;
        ShaderModule &operator=(const ShaderModule &) = delete;
        ShaderModule(ShaderModule &&) noexcept;
        ShaderModule &operator=(ShaderModule &&) noexcept;

        /// @brief The portable handle, for a pipeline build.
        ShaderModuleHandle GetHandle() const noexcept;
    };
} // namespace Engine::Rhi

#endif // ENGINE_RHI_SHADER_MODULE_INCLUDED
