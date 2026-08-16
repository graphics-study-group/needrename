#include "reflection_lifecycle.h"

#include "meta_annorefl_reflection_lifecycle/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionLifecycleTest() {
    auto lifecycle_type = AnnoRefl::GetType("LifecycleTest");

    LifecycleTest::ResetCounters();
    assert(LifecycleTest::constructed == 0);
    assert(LifecycleTest::destructed == 0);
    assert(LifecycleTest::alive == 0);
    assert(LifecycleTest::InnerProbe::alive == 0);
    assert(LifecycleTest::InnerProbe::destroyed == 0);

    {
        AnnoRefl::Var v = lifecycle_type->CreateInstance();
        assert(LifecycleTest::constructed == 1);
        assert(LifecycleTest::alive == 1);

        auto &obj = v.Get<LifecycleTest>();
        assert(obj.m_probe != nullptr);
        assert(LifecycleTest::InnerProbe::alive == 1);

        AnnoRefl::Var ret = v.InvokeMethod("MakeAnother");
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
