#include "PhysicsSpirvLoader.h"

#include <cmake_config.h>

#include <Core/FileSystem/DataRoot.h>
#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>
#include <Rhi/Pipeline/ShaderInterfaceDescription.h>
#include <Rhi/Pipeline/ShaderModule.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

namespace Engine {
    std::filesystem::path PhysicsSpirvDir() {
        return ResolveDataDir(ENGINE_DATA_SUBDIR_PHYSICS_SPIRV, ENGINE_PHYSICS_SPIRV_DIR);
    }

    std::vector<uint32_t> LoadPhysicsSpirv(std::string_view relative_path) {
        const std::filesystem::path full = PhysicsSpirvDir() / std::filesystem::path(std::string(relative_path));

        std::ifstream file(full, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open physics SPIR-V: " + full.string());
        }

        const auto size = static_cast<size_t>(file.tellg());
        if (size == 0u || size % sizeof(uint32_t) != 0u) {
            throw std::runtime_error("Invalid physics SPIR-V size: " + full.string());
        }

        std::vector<uint32_t> words(size / sizeof(uint32_t));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char *>(words.data()), static_cast<std::streamsize>(size));
        return words;
    }

    Rhi::ComputeKernel &LoadPhysicsKernel(
        Rhi::DeviceContext &device_context, std::string_view relative_path, std::string_view debug_name
    ) {
        if (Rhi::ComputeKernel *existing = device_context.FindComputeKernel(relative_path)) {
            return *existing;
        }
#if defined(ENGINE_RHI_BACKEND_WEBGPU)
        // This backend loads the baked form of the module: the WGSL the bake produced next to
        // the SPIR-V, and the interface description that says what the module declares. It has
        // no reflection library, which is why both are build artifacts rather than something
        // derived here.
        const std::filesystem::path stem =
            PhysicsSpirvDir() / std::filesystem::path{std::string(relative_path).substr(0, std::string(relative_path).size() - 4)};
        const std::filesystem::path wgsl_path = stem.string() + ".wgsl";
        const std::filesystem::path description_path = stem.string() + ".iface.json";

        std::ifstream wgsl_file(wgsl_path);
        if (!wgsl_file.is_open()) {
            throw std::runtime_error(
                "Failed to open the baked WGSL for a physics kernel: " + wgsl_path.string()
                + " (the WebGPU build bakes it next to the SPIR-V; see physics_shader_wgsl)."
            );
        }
        std::string wgsl((std::istreambuf_iterator<char>(wgsl_file)), std::istreambuf_iterator<char>());

        const std::optional<Rhi::ShaderInterfaceDescription> description =
            Rhi::ShaderInterfaceDescription::FromFile(description_path);
        if (!description.has_value()) {
            throw std::runtime_error(
                "Failed to read the interface description of a physics kernel: " + description_path.string()
            );
        }

        const Rhi::ShaderModule module = Rhi::ShaderModule::Create(
            device_context.GetDeviceInterface(),
            std::as_bytes(std::span{wgsl.data(), wgsl.size()}),
            debug_name
        );
        return device_context.RequestComputeKernelFromModule(
            relative_path, module.GetHandle(), description.value(), description->entry_point, debug_name
        );
#else
        return device_context.RequestComputeKernel(relative_path, LoadPhysicsSpirv(relative_path), debug_name);
#endif
    }
} // namespace Engine
