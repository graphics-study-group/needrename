#include "reflection_inheritance.h"

#include "meta_annorefl_reflection_inheritance/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionInheritanceTest() {
    auto virtual_base_type = AnnoRefl::GetType("VirtualBase");
    auto virtual_derived1_type = AnnoRefl::GetType("VirtualDerived1");
    auto virtual_derived2_type = AnnoRefl::GetType("VirtualDerived2");
    auto virtual_diamond_type = AnnoRefl::GetType("VirtualDiamond");
    auto nonvirtual_base_type = AnnoRefl::GetType("NonVirtualBase");
    auto nonvirtual_derived_type = AnnoRefl::GetType("NonVirtualDerived");

    assert(virtual_derived1_type->IsDerivedFrom(virtual_base_type));
    assert(virtual_derived2_type->IsDerivedFrom(virtual_base_type));
    assert(virtual_diamond_type->IsDerivedFrom(virtual_base_type));
    assert(virtual_diamond_type->IsDerivedFrom(virtual_derived1_type));
    assert(virtual_diamond_type->IsDerivedFrom(virtual_derived2_type));

    assert(nonvirtual_derived_type->IsDerivedFrom(nonvirtual_base_type));

    assert(!virtual_base_type->IsDerivedFrom(virtual_derived1_type));
    assert(!virtual_derived1_type->IsDerivedFrom(virtual_diamond_type));
    assert(!virtual_base_type->IsDerivedFrom(virtual_base_type));

    auto fooa_type = AnnoRefl::GetType("FooA");
    auto foobase_type = AnnoRefl::GetType("FooBase");
    auto bbase_type = AnnoRefl::GetType("BBase");
    assert(fooa_type->IsDerivedFrom(foobase_type));
    assert(fooa_type->IsDerivedFrom(bbase_type));
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionInheritanceTest();
    return 0;
}

#include "__generated__/reflection_inheritance.h.inc"
