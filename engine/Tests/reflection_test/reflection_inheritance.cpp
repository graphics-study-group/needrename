#include "reflection_inheritance.h"

#include "meta_reflection_inheritance/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionInheritanceTest() {
    auto virtual_base_type = Engine::Reflection::GetType("VirtualBase");
    auto virtual_derived1_type = Engine::Reflection::GetType("VirtualDerived1");
    auto virtual_derived2_type = Engine::Reflection::GetType("VirtualDerived2");
    auto virtual_diamond_type = Engine::Reflection::GetType("VirtualDiamond");
    auto nonvirtual_base_type = Engine::Reflection::GetType("NonVirtualBase");
    auto nonvirtual_derived_type = Engine::Reflection::GetType("NonVirtualDerived");

    assert(virtual_derived1_type->IsDerivedFrom(virtual_base_type));
    assert(virtual_derived2_type->IsDerivedFrom(virtual_base_type));
    assert(virtual_diamond_type->IsDerivedFrom(virtual_base_type));
    assert(virtual_diamond_type->IsDerivedFrom(virtual_derived1_type));
    assert(virtual_diamond_type->IsDerivedFrom(virtual_derived2_type));

    assert(nonvirtual_derived_type->IsDerivedFrom(nonvirtual_base_type));

    assert(!virtual_base_type->IsDerivedFrom(virtual_derived1_type));
    assert(!virtual_derived1_type->IsDerivedFrom(virtual_diamond_type));
    assert(!virtual_base_type->IsDerivedFrom(virtual_base_type));

    auto fooa_type = Engine::Reflection::GetType("FooA");
    auto foobase_type = Engine::Reflection::GetType("FooBase");
    auto bbase_type = Engine::Reflection::GetType("BBase");
    assert(fooa_type->IsDerivedFrom(foobase_type));
    assert(fooa_type->IsDerivedFrom(bbase_type));
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionInheritanceTest();
    return 0;
}

#include "__generated__/reflection_inheritance.h.inc"
