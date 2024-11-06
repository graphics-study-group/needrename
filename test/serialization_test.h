#ifndef CTEST_SERIALIZATION_TEST_H
#define CTEST_SERIALIZATION_TEST_H

#include <memory>
#include <vector>
#include "Reflection/reflection.h"

namespace SerializationTest
{
    class REFL_SER_CLASS(REFL_BLACKLIST) BaseData
    {
    public:
        BaseData() = default;
        virtual ~BaseData() = default;

        float data[100] = {0.0f};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) InheritTest : public BaseData
    {
    public:
        InheritTest() = default;
        virtual ~InheritTest() = default;

        int m_inherit = 1000;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) SharedPtrTest
    {
    public:
        SharedPtrTest() = default;
        virtual ~SharedPtrTest() = default;

        std::shared_ptr<BaseData> m_shared_ptr {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) PtrTest
    {
    public:
        PtrTest() = default;
        virtual ~PtrTest() = default;

        BaseData *m_ptr = nullptr;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) VectorTest
    {
    public:
        VectorTest() = default;
        virtual ~VectorTest() = default;

        std::vector<BaseData> m_vector {};
    };
}

#endif // CTEST_SERIALIZATION_TEST_H
