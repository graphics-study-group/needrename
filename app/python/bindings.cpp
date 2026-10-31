/**
 * @file bindings.cpp
 *
 * @brief nanobind extension module `needrename_sim._physicsapp`.
 *
 * The module is the binary half of the `needrename-sim` distribution package:
 * `needrename_sim/__init__.py` resolves the engine DLLs and the runtime data
 * root, then imports this module.
 */

#include "binding_support.h"

#include <stdexcept>

namespace {
    /// @brief Translate the C++ exception types the PhysicsApp API documents.
    ///
    /// nanobind's own exceptions (`nb::builtin_exception`, `nb::python_error`)
    /// are rethrown so nanobind's built-in translators keep handling them.
    void TranslatePhysicsAppExceptions(const std::exception_ptr &error, void *) {
        try {
            std::rethrow_exception(error);
        } catch (const nb::python_error &) {
            throw;
        } catch (const nb::builtin_exception &) {
            throw;
        } catch (const std::invalid_argument &e) {
            PyErr_SetString(PyExc_ValueError, e.what());
        } catch (const std::out_of_range &e) {
            PyErr_SetString(PyExc_IndexError, e.what());
        } catch (const std::logic_error &e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
        } catch (const std::exception &e) {
            PyErr_SetString(PyExc_RuntimeError, e.what());
        }
    }
} // namespace

NB_MODULE(_physicsapp, m) {
    m.doc() = "nanobind bindings for the needrename PhysicsApp GPU physics simulator";
    m.attr("__version__") = "0.1.0";

    nb::register_exception_translator(&TranslatePhysicsAppExceptions);

    NeedrenameBindings::RegisterGlmTypes(m);
    NeedrenameBindings::RegisterValueTypes(m);
    NeedrenameBindings::RegisterPhysicsApp(m);
}
