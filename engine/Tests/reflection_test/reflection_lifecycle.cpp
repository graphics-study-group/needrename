#include "reflection_lifecycle.h"

#include "meta_reflection_lifecycle/reflection_init.inc"
#include <Reflection/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        Engine::Reflection::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionLifecycleTest() {
    auto lifecycle_type = Engine::Reflection::GetType("LifecycleTest");

    LifecycleTest::ResetCounters();
    assert(LifecycleTest::constructed == 0);
    assert(LifecycleTest::destructed == 0);
    assert(LifecycleTest::alive == 0);
    assert(LifecycleTest::InnerProbe::alive == 0);
    assert(LifecycleTest::InnerProbe::destroyed == 0);

    {
        Engine::Reflection::Var v = lifecycle_type->CreateInstance();
        assert(LifecycleTest::constructed == 1);
        assert(LifecycleTest::alive == 1);

        auto &obj = v.Get<LifecycleTest>();
        assert(obj.m_probe != nullptr);
        assert(LifecycleTest::InnerProbe::alive == 1);

        Engine::Reflection::Var ret = v.InvokeMethod("MakeAnother");
        assert(LifecycleTest::alive == 2);

        auto &ret_obj = ret.Get<LifecycleTest>();
        assert(ret_obj.m_probe != nullptr);
        assert(LifecycleTest::InnerProbe::alive == 2);
    }

    assert(LifecycleTest::alive == 0);
    assert(LifecycleTest::InnerProbe::alive == 0);
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionLifecycleTest();
    return 0;
}

#include "__generated__/reflection_lifecycle.h.inc"
