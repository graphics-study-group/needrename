// Tests for the shared physics SPIR-V loader (task 2.1):
//   - a present module loads and yields a SPIR-V module
//   - a missing module throws `std::runtime_error` naming the absolute path
//     attempted
//
// The test deliberately does not include `ENGINE_PHYSICS_SPIRV_DIR`: it must not
// need the per-configuration subdirectory definition that the loader carries.

#include "Physics/PhysicsSpirvLoader.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Engine;

static bool g_pass = true;
#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl;                                        \
            g_pass = false;                                                                                            \
        }                                                                                                              \
    } while (0)

int main() {
    // ── The success path: a module the build produces ───────────────────────

    bool loaded = false;
    try {
        const std::vector<uint32_t> words = LoadPhysicsSpirv("solver/XPBDSolver/integrate_forces.comp.spv");
        loaded = !words.empty() && words.front() == 0x07230203u;
    } catch (const std::exception &e) {
        std::cerr << "Unexpected failure loading a present module: " << e.what() << std::endl;
    }
    CHECK(loaded && "a present physics module must load as SPIR-V words");

    // ── The failure contract: a missing module ──────────────────────────────

    bool threw = false;
    try {
        LoadPhysicsSpirv("solver/XPBDSolver/this_module_does_not_exist.comp.spv");
    } catch (const std::runtime_error &e) {
        const std::string message = e.what();
        const auto separator = message.find(": ");
        const std::string attempted = separator == std::string::npos ? std::string{} : message.substr(separator + 2);
        threw = separator != std::string::npos
                && message.find("this_module_does_not_exist.comp.spv") != std::string::npos
                && std::filesystem::path(attempted).is_absolute();
        if (!threw) {
            std::cerr << "The diagnostic did not name the absolute path attempted: " << message << std::endl;
        }
    } catch (const std::exception &e) {
        std::cerr << "Expected std::runtime_error, got: " << e.what() << std::endl;
    }
    CHECK(threw && "a missing physics module must throw std::runtime_error naming the absolute path attempted");

    if (!g_pass) {
        std::cerr << "Physics SPIR-V loader test FAILED." << std::endl;
        return 1;
    }
    std::cout << "Physics SPIR-V loader test PASSED." << std::endl;
    return 0;
}
