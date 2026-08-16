#include "reflection_namespace.h"

#include "meta_annorefl_reflection_namespace/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionNamespaceTest() {
    AnnoRefl::Var ns_test = AnnoRefl::GetType("NamespaceTest")->CreateInstance();
    ns_test.InvokeMethod("PrintInfo");
    assert(ns_test.GetDataPtr() == NamespaceTest_PrintInfo_Called);

    AnnoRefl::Var ns_test2 = AnnoRefl::GetType("TestHelloWorld::NamespaceTest")->CreateInstance();
    ns_test2.InvokeMethod("PrintInfo");
    assert(ns_test2.GetDataPtr() == TestHelloWorld_NamespaceTest_PrintInfo_Called);

    AnnoRefl::Var ns_test3 =
        AnnoRefl::GetType("TestHelloWorld::TestHelloWorld2::NamespaceTest")->CreateInstance();
    ns_test3.InvokeMethod("PrintInfo");
    assert(ns_test3.GetDataPtr() == TestHelloWorld_TestHelloWorld2_NamespaceTest_PrintInfo_Called);
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionNamespaceTest();
    return 0;
}

#include "__generated__/reflection_namespace.h.inc"
