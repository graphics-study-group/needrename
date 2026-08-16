#include "reflection_enum.h"

#include "meta_annorefl_reflection_enum/reflection_init.inc"
#include <AnnoRefl/Type.h>
#include <AnnoRefl/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionEnumTest() {
    AnnoRefl::Var enum_class_test = AnnoRefl::GetType("EnumClassTest")->CreateInstance();

    auto color_var = enum_class_test.GetMember("m_color");
    assert(color_var.GetType()->GetTypeKind() == AnnoRefl::Type::TypeKind::Enum);
    assert(color_var.GetEnumString() == "Red");
    color_var.SetEnumFromString("Green");
    assert(color_var.GetEnumString() == "Green");

    auto small_var = enum_class_test.GetMember("m_small");
    auto big_var = enum_class_test.GetMember("m_big");
    assert(small_var.GetType()->GetTypeKind() == AnnoRefl::Type::TypeKind::Enum);
    assert(big_var.GetType()->GetTypeKind() == AnnoRefl::Type::TypeKind::Enum);
    assert(small_var.GetEnumString() == "A");
    assert(big_var.GetEnumString() == "Big");

    small_var.SetEnumFromString("C");
    big_var.SetEnumFromString("One");
    assert(small_var.GetEnumString() == "C");
    assert(big_var.GetEnumString() == "One");
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionEnumTest();
    return 0;
}

#include "__generated__/reflection_enum.h.inc"
