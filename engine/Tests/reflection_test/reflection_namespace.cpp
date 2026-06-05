#include "reflection_namespace.h"

#include "meta_reflection_namespace/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionNamespaceTest() {
    Engine::Reflection::Var ns_test = Engine::Reflection::GetType("NamespaceTest")->CreateInstance();
    ns_test.InvokeMethod("PrintInfo");
    assert(ns_test.GetDataPtr() == NamespaceTest_PrintInfo_Called);

    Engine::Reflection::Var ns_test2 = Engine::Reflection::GetType("TestHelloWorld::NamespaceTest")->CreateInstance();
    ns_test2.InvokeMethod("PrintInfo");
    assert(ns_test2.GetDataPtr() == TestHelloWorld_NamespaceTest_PrintInfo_Called);

    Engine::Reflection::Var ns_test3 =
        Engine::Reflection::GetType("TestHelloWorld::TestHelloWorld2::NamespaceTest")->CreateInstance();
    ns_test3.InvokeMethod("PrintInfo");
    assert(ns_test3.GetDataPtr() == TestHelloWorld_TestHelloWorld2_NamespaceTest_PrintInfo_Called);
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionNamespaceTest();
    return 0;
}

#include "__generated__/reflection_namespace.h.inc"
