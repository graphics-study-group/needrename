#ifndef CTEST_SERIALIZATION_TEST_H
#define CTEST_SERIALIZATION_TEST_H

#include <memory>
#include <vector>
#include <meta_serialization_test/reflection.hpp>

namespace SerializationTest
{
    class REFL_SER_CLASS(REFL_BLACKLIST) BaseData
    {
        REFL_SER_BODY()
    public:
        BaseData() = default;
        virtual ~BaseData() = default;

        float data[3] = {0.0f};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) InheritTest : public BaseData
    {
        REFL_SER_BODY()
    public:
        InheritTest() = default;
        virtual ~InheritTest() = default;

        int m_inherit = 1000;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) SharedPtrTest
    {
        REFL_SER_BODY()
    public:
        SharedPtrTest() = default;
        virtual ~SharedPtrTest() = default;

        std::shared_ptr<BaseData> m_shared_ptr {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) PtrTest
    {
        REFL_SER_BODY()
    public:
        PtrTest() = default;
        virtual ~PtrTest() = default;

        BaseData *m_ptr = nullptr;
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) VectorTest
    {
        REFL_SER_BODY()
    public:
        VectorTest() = default;
        virtual ~VectorTest() = default;

        std::vector<BaseData> m_vector {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) PolymorphismTest
    {
        REFL_SER_BODY()
    public:
        PolymorphismTest() = default;
        virtual ~PolymorphismTest() = default;

        std::vector<std::shared_ptr<BaseData>> m_vector {};
    };

    class REFL_SER_CLASS(REFL_BLACKLIST) CustomTest
    {
        REFL_SER_BODY()
    public:
        CustomTest() = default;
        virtual ~CustomTest() = default;

        int m_a = 621;
        int m_b = 182376;

        REFL_SER_DISABLE inline void save(Engine::Serialization::Archive& buffer) const
        {
            buffer.json["data"] = m_a * 1000000 + m_b;
        }

        REFL_SER_DISABLE inline void load(Engine::Serialization::Archive& buffer)
        {
            m_a = buffer.json["data"].get<int>() / 1000000;
            m_b = buffer.json["data"].get<int>() % 1000000;
        }
    };
}

#endif // CTEST_SERIALIZATION_TEST_H
