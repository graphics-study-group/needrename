#ifndef CTEST_SERIALIZATION_POLYMORPHISM_H
#define CTEST_SERIALIZATION_POLYMORPHISM_H

#include <Reflection/macros.h>
#include <Reflection/serialization_smart_pointer.h>
#include <Reflection/serialization_vector.h>
#include <memory>
#include <vector>

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

    class REFL_SER_CLASS(REFL_BLACKLIST) PolymorphismTest {
        REFL_SER_BODY(PolymorphismTest)
    public:
        PolymorphismTest() = default;
        virtual ~PolymorphismTest() = default;

        std::vector<std::shared_ptr<BaseData>> m_vector{};
    };
} // namespace SerializationTest

void RunSerializationPolymorphismTest();

#endif // CTEST_SERIALIZATION_POLYMORPHISM_H
