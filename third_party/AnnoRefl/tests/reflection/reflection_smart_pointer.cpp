#include "reflection_smart_pointer.h"

#include "meta_annorefl_reflection_smart_pointer/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <cassert>

namespace {
    void InitializeReflectionRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunReflectionSmartPointerTest() {
    AnnoRefl::Var smart_ptr_test = AnnoRefl::GetType("SmartPointerTest")->CreateInstance();
    assert(smart_ptr_test.GetMember("m_shared_ptr").GetPointedVar().Get<int>() == 42);
    assert(smart_ptr_test.GetMember("m_weak_ptr").GetPointedVar().Get<int>() == 42);
    assert(smart_ptr_test.GetMember("m_unique_ptr").GetPointedVar().Get<float>() == 84.0f);

    smart_ptr_test.GetMember("m_shared_ptr").GetPointedVar().Set<int>(2);
    assert(smart_ptr_test.GetMember("m_shared_ptr").GetPointedVar().Get<int>() == 2);
    assert(smart_ptr_test.GetMember("m_weak_ptr").GetPointedVar().Get<int>() == 2);
}

int main() {
    InitializeReflectionRuntime();
    RunReflectionSmartPointerTest();
    return 0;
}

#include "__generated__/reflection_smart_pointer.h.inc"
