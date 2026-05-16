#ifndef CTEST_SERIALIZATION_SELF_PTR_H
#define CTEST_SERIALIZATION_SELF_PTR_H

#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <memory>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_BLACKLIST) SelfPtrTest {
        REFL_SER_BODY(SelfPtrTest)
    public:
        SelfPtrTest() = default;
        virtual ~SelfPtrTest() = default;

        std::shared_ptr<SelfPtrTest> m_self_ptr{};
    };
} // namespace SerializationTest

void RunSerializationSelfPtrTest();

#endif // CTEST_SERIALIZATION_SELF_PTR_H
