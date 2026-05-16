#ifndef CTEST_SERIALIZATION_SHARED_PTR_H
#define CTEST_SERIALIZATION_SHARED_PTR_H

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

    class REFL_SER_CLASS(REFL_BLACKLIST) SharedPtrTest {
        REFL_SER_BODY(SharedPtrTest)
    public:
        SharedPtrTest() = default;
        virtual ~SharedPtrTest() = default;

        std::shared_ptr<BaseData> m_shared_ptr{};
        std::shared_ptr<BaseData> m_shared_ptr2{};
        std::shared_ptr<int> m_int_ptr{};
        std::weak_ptr<BaseData> m_weak_ptr{};
    };
} // namespace SerializationTest

void RunSerializationSharedPtrTest();

#endif // CTEST_SERIALIZATION_SHARED_PTR_H
