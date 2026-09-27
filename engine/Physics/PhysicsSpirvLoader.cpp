#include "PhysicsSpirvLoader.h"

#include <cmake_config.h>

#include <Rhi/Device/DeviceContext.h>
#include <Rhi/Pipeline/ComputeKernel.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace Engine {
    std::vector<uint32_t> LoadPhysicsSpirv(std::string_view relative_path) {
        const std::filesystem::path full =
            std::filesystem::path(ENGINE_PHYSICS_SPIRV_DIR) / std::filesystem::path(std::string(relative_path));

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
        return device_context.RequestComputeKernel(relative_path, LoadPhysicsSpirv(relative_path), debug_name);
    }
} // namespace Engine
