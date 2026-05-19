#include "reflection_nested_member.h"

#include "meta_reflection_nested_member/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>
#include <string>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionNestedMemberTest() {
    Engine::Reflection::Var top_level_var = Engine::Reflection::GetType("TopLevelStruct")->CreateInstance();

    top_level_var.GetMember("top_value").Set(999);
    assert(top_level_var.GetMember("top_value").Get<int>() == 999);

    top_level_var.GetMemberRecursively("middle_member.middle_value").Set(888);
    assert(top_level_var.GetMemberRecursively("middle_member.middle_value").Get<int>() == 888);
    assert(top_level_var.GetMember("middle_member").GetMember("middle_value").Get<int>() == 888);

    top_level_var.GetMemberRecursively("middle_member.nested_member.level1_value").Set(777);
    assert(top_level_var.GetMemberRecursively("middle_member.nested_member.level1_value").Get<int>() == 777);
    assert(
        top_level_var.GetMember("middle_member").GetMember("nested_member").GetMember("level1_value").Get<int>() == 777
    );

    top_level_var.GetMemberRecursively("middle_member.nested_member.level1_name").Set(std::string("recursive_test"));
    assert(
        top_level_var.GetMemberRecursively("middle_member.nested_member.level1_name").Get<std::string>()
        == "recursive_test"
    );

    top_level_var.GetMemberRecursively("top_value").Set(111);
    assert(top_level_var.GetMemberRecursively("top_value").Get<int>() == 111);

    top_level_var.GetMemberRecursively("top_name").Set(std::string("recursive_top"));
    top_level_var.GetMemberRecursively("middle_member.middle_name").Set(std::string("recursive_middle"));
    assert(top_level_var.GetMemberRecursively("top_name").Get<std::string>() == "recursive_top");
    assert(top_level_var.GetMemberRecursively("middle_member.middle_name").Get<std::string>() == "recursive_middle");
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionNestedMemberTest();
    return 0;
}

#include "__generated__/reflection_nested_member.h.inc"
