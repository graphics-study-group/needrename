#include "external_component.h"

#include <meta_sdk_consumer_reflection/reflection_init.inc>

#include <AnnoRefl/reflection.h>
#include <AnnoRefl/serialization.h>
#include <AnnoRefl/Type.h>

#include <cmath>
#include <cstdio>
#include <exception>
#include <iostream>
#include <string>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace {
    // A consumer binary must not pop an abort dialog when it fails; report to
    // stderr and exit with a non-zero code instead.
    void ReportTerminate() {
        const std::exception_ptr error = std::current_exception();
        try {
            if (error) {
                std::rethrow_exception(error);
            }
            std::cerr << "terminate without exception\n";
        } catch (const std::exception &exception) {
            std::cerr << "terminate: " << exception.what() << '\n';
        } catch (...) {
            std::cerr << "terminate: unknown exception\n";
        }
        std::_Exit(7);
    }
} // namespace

// Implemented by the engine's core module and exported to consumers; registers
// the engine's own reflected types before the consumer's registrar runs.
extern "C" void RegisterCoreTypes();

int main() {
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    std::set_terminate(ReportTerminate);

    AnnoRefl::Initialize();
    RegisterCoreTypes();
    RegisterAllTypes();

    // The shipped type lists must have taught the generator about the engine
    // base type; without them the consumer class could not register its base.
    const auto engine_type = AnnoRefl::Type::s_name_index_map.find("Engine::Transform");
    std::cout << "engine base type   : Engine::Transform "
              << (engine_type == AnnoRefl::Type::s_name_index_map.end() ? "NOT registered" : "registered") << '\n';

    ExternalSpinner spinner;
    spinner.m_speed = 42.5f;
    spinner.m_samples = {4.0f, 5.0f, 6.0f};
    spinner.SetPosition({1.0f, 2.0f, 3.0f});

    AnnoRefl::Archive archive;
    AnnoRefl::serialize(spinner, archive);

    ExternalSpinner loaded;
    AnnoRefl::deserialize(loaded, archive);

    const bool ok = engine_type != AnnoRefl::Type::s_name_index_map.end()
                 && std::fabs(loaded.m_speed - 42.5f) < 1e-6f
                 && loaded.m_samples.size() == 3
                 && loaded.m_samples[2] == 6.0f
                 && std::fabs(loaded.GetPosition().z - 3.0f) < 1e-6f;
    std::printf(
        "round trip         : speed=%.1f samples=%zu z=%.1f -> %s\n",
        loaded.m_speed,
        loaded.m_samples.size(),
        loaded.GetPosition().z,
        ok ? "PASSED" : "FAILED"
    );
    return ok ? 0 : 1;
}

#include "__generated__/external_component.h.inc"
