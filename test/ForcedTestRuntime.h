// What every test process does before `main`.
//
// Two ways a failing test used to look like a hung one, both from the C runtime:
//
// - a failed assertion is reported by opening a message box, and nothing in a test run can dismiss
//   one, so the process waits on a dialog nobody can see;
// - an exception that leaves `main` reaches `std::terminate`, which the CRT also reports through a
//   dialog.
//
// A test harness needs a line and a non-zero exit instead. Assertions go to stderr, and the
// terminate handler prints what was thrown and leaves the process immediately - `abort` would open
// the very dialog this file exists to avoid.

#pragma once

#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <stdlib.h>

namespace Engine::TestSupport {
    namespace {
        [[noreturn]] void ReportTermination() {
            std::fprintf(stderr, "terminate called");
            if (const std::exception_ptr thrown = std::current_exception(); thrown != nullptr) {
                try {
                    std::rethrow_exception(thrown);
                } catch (const std::exception &error) {
                    std::fprintf(stderr, ": %s", error.what());
                } catch (...) {
                    std::fprintf(stderr, ": unknown exception");
                }
            }
            std::fprintf(stderr, "\n");
            std::fflush(stderr);
            std::_Exit(3);
        }

        struct TestRuntime {
            TestRuntime() {
                _set_error_mode(_OUT_TO_STDERR);
                std::set_terminate(&ReportTermination);
            }
        };

        const TestRuntime test_runtime{};
    } // namespace
} // namespace Engine::TestSupport
#endif
