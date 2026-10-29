#ifndef ENGINE_PHYSICS_PHYSICSSPIRVLOADER_INCLUDED
#define ENGINE_PHYSICS_PHYSICSSPIRVLOADER_INCLUDED

#include "physics_export.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace Engine::Rhi {
    class ComputeKernel;
    class DeviceContext;
} // namespace Engine::Rhi

namespace Engine {
    /**
     * @brief Get the physics SPIR-V root of the resolved runtime data root.
     *
     * An installed data root keeps the modules in `spv/` with no configuration
     * split; the developer build tree keeps the per-configuration build output
     * directory named by `ENGINE_PHYSICS_SPIRV_DIR`.
     *
     * @return The directory holding the physics compute SPIR-V modules.
     * @exception std::runtime_error The directory does not exist.
     */
    PHYSICS_API std::filesystem::path PhysicsSpirvDir();

    /**
     * @brief Read a precompiled physics shader from the physics SPIR-V tree.
     *
     * This is the single loader every physics component uses: the solver, both
     * detectors and the `gpu_algorithm` classes.
     *
     * @param relative_path Path of the module relative to the physics SPIR-V
     * root, e.g. `solver/XPBDSolver/step.comp.spv`.
     * @return The module's SPIR-V words.
     * @exception std::runtime_error The file is missing, empty, or its size is
     * not a multiple of four bytes. The message names the absolute path
     * attempted.
     */
    PHYSICS_API std::vector<uint32_t> LoadPhysicsSpirv(std::string_view relative_path);

    /**
     * @brief Get the compute kernel for a physics shader, loading it on first use.
     *
     * The module's source-relative path is its kernel identity, so two
     * components that use the same shader share one kernel — and a component
     * whose shader another already loaded reads no file at all.
     *
     * @param device_context The device to create the kernel on.
     * @param relative_path Path of the module relative to the physics SPIR-V root.
     * @param debug_name Debug name applied to the kernel when it is created.
     * @exception std::runtime_error The module is missing, empty, or its size is
     * not a multiple of four bytes; see `LoadPhysicsSpirv`.
     */
    PHYSICS_API Rhi::ComputeKernel &LoadPhysicsKernel(
        Rhi::DeviceContext &device_context, std::string_view relative_path, std::string_view debug_name = {}
    );
} // namespace Engine

#endif // ENGINE_PHYSICS_PHYSICSSPIRVLOADER_INCLUDED
