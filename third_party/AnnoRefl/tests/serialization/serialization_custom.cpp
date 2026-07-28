#include "serialization_custom.h"

#include "meta_annorefl_serialization_custom/reflection_init.inc"
#include <AnnoRefl/reflection.h>
#include <AnnoRefl/serialization.h>
#include <cassert>

namespace SerializationTest {
    void CustomTest::save_to_archive(AnnoRefl::Archive &archive) const {
        AnnoRefl::Json &json = *archive.m_cursor;
        json["data"] = m_a * 1000000 + m_b;
    }

    void CustomTest::load_from_archive(AnnoRefl::Archive &archive) {
        AnnoRefl::Json &json = *archive.m_cursor;
        m_a = json["data"].get<int>() / 1000000;
        m_b = json["data"].get<int>() % 1000000;
    }
} // namespace SerializationTest

namespace {
    void InitializeSerializationRuntime() {
        AnnoRefl::Initialize();
        RegisterAllTypes();
    }
} // namespace

void RunSerializationCustomTest() {
    using namespace SerializationTest;

    CustomTest custom_test;
    custom_test.m_a = 123;
    custom_test.m_b = 852765;

    AnnoRefl::Archive archive;
    AnnoRefl::serialize(custom_test, archive);

    custom_test.m_a = 0;
    custom_test.m_b = 0;

    CustomTest custom_test2;
    AnnoRefl::deserialize(custom_test2, archive);

    assert(custom_test2.m_a == 123);
    assert(custom_test2.m_b == 852765);
}

int main() {
    InitializeSerializationRuntime();
    RunSerializationCustomTest();
    return 0;
}

#include "__generated__/serialization_custom.h.inc"
