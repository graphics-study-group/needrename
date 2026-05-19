#ifndef CTEST_SERIALIZATION_ARRAY_PTR_H
#define CTEST_SERIALIZATION_ARRAY_PTR_H

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

    class REFL_SER_CLASS(REFL_BLACKLIST) InheritTest : public BaseData {
        REFL_SER_BODY(InheritTest)
    public:
        InheritTest() = default;
        virtual ~InheritTest() = default;

        int m_inherit = 1000;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) ArrayPtrTest {
        REFL_SER_BODY(ArrayPtrTest)
    public:
        ArrayPtrTest() = default;
        virtual ~ArrayPtrTest() = default;

        int m_array[3] = {1, 2, 3};
        std::shared_ptr<BaseData> m_ptr_array[2][2] = {{nullptr, nullptr}, {nullptr, nullptr}};
    };
} // namespace SerializationTest

void RunSerializationArrayPtrTest();

#endif // CTEST_SERIALIZATION_ARRAY_PTR_H
