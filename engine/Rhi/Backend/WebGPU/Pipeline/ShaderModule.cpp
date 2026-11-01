#include "Rhi/Pipeline/ShaderModule.h"

#include "Rhi/Backend/WebGPU/WgpuHandles.h"
#include "Rhi/Device/DeviceInterface.h"

#include <SDL3/SDL.h>
#include <string_view>
#include <webgpu/webgpu.h>

namespace Engine::Rhi {
    /**
     * @brief A module this backend can hand to a pipeline.
     *
     * The seam's `Create` takes "the compiled code" as bytes. On the Vulkan side that is
     * SPIR-V; here it is WGSL text, which is what the bake emits for a backend whose
     * reflection happens at build time (D9). Both are "the code the device consumes",
     * so the shared signature holds - a caller that has one form does not have the
     * other, and the backend it is building against decides which it was given.
     *
     * The bytes are not required to be null-terminated; the view's length is used, which
     * is why the source is copied into a string rather than read as a C string.
     */
    struct ShaderModule::impl {
        WGPUShaderModule module{nullptr};
    };

    ShaderModule::ShaderModule(std::unique_ptr<impl> p) noexcept : pimpl(std::move(p)) {
    }

    ShaderModule ShaderModule::Create(
        const DeviceInterface &device_interface, std::span<const std::byte> code, std::string_view name
    ) {
        auto state = std::make_unique<impl>();

        const auto *text = reinterpret_cast<const char *>(code.data());
        const std::string wgsl{text, code.size()};

        WGPUShaderSourceWGSL wgsl_desc{};
        wgsl_desc.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl_desc.code = WGPUStringView{wgsl.data(), wgsl.size()};

        WGPUShaderModuleDescriptor desc{};
        desc.nextInChain = &wgsl_desc.chain;
        desc.label = WGPUStringView{name.data(), name.size()};

        state->module = wgpuDeviceCreateShaderModule(ToWgpuDevice(device_interface.GetDevice()), &desc);
        if (state->module == nullptr) {
            // The device reports the detailed error through its uncaptured-error
            // callback; a null module is this level's summary of it.
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "WebGPU: shader module creation failed.");
        }
        return ShaderModule(std::move(state));
    }

    ShaderModule::ShaderModule(ShaderModule &&) noexcept = default;
    ShaderModule &ShaderModule::operator=(ShaderModule &&) noexcept = default;

    ShaderModule::~ShaderModule() {
        if (pimpl && pimpl->module != nullptr) {
            wgpuShaderModuleRelease(pimpl->module);
            pimpl->module = nullptr;
        }
    }

    ShaderModuleHandle ShaderModule::GetHandle() const noexcept {
        return FromWgpuShaderModule(pimpl->module);
    }
} // namespace Engine::Rhi

