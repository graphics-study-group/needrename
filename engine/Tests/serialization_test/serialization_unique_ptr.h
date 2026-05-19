#ifndef CTEST_SERIALIZATION_UNIQUE_PTR_H
#define CTEST_SERIALIZATION_UNIQUE_PTR_H

#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <memory>

namespace SerializationTest {
    class REFL_SER_CLASS(REFL_BLACKLIST) BaseData {
        REFL_SER_BODY(BaseData)
    public:
        BaseData() = default;
        virtual ~BaseData() = default;

        float data[3] = {0.0f};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) UniquePtrTest {
        REFL_SER_BODY(UniquePtrTest)
    public:
        UniquePtrTest() = default;
        virtual ~UniquePtrTest() = default;

        std::unique_ptr<BaseData> m_unique_ptr{};
    };
} // namespace SerializationTest

void RunSerializationUniquePtrTest();

#endif // CTEST_SERIALIZATION_UNIQUE_PTR_H
