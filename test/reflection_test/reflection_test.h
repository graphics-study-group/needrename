#ifndef CTEST_REFLECTION_TEST_H
#define CTEST_REFLECTION_TEST_H

#include <cstdint>
#include <vector>
#include <Reflection/macros.h>

class REFL_SER_CLASS(REFL_BLACKLIST) Test_stdint
{
    REFL_SER_BODY(Test_stdint)
public:
    Test_stdint() = default;
    virtual ~Test_stdint() = default;

    int8_t m_int8 = 0;
    int16_t m_int16 = 0;
    int32_t m_int32 = 0;
    int64_t m_int64 = 0;
    uint8_t m_uint8 = 0;
    uint16_t m_uint16 = 0;
    uint32_t m_uint32 = 0;
    uint64_t m_uint64 = 0;
};

class REFL_SER_CLASS(REFL_BLACKLIST) FooBase
{
    REFL_SER_BODY(FooBase)
public:
    FooBase() = default;
    virtual ~FooBase() = default;

    int m_foobase = 1000;

    virtual void PrintHelloWorld() const;
};

class REFL_SER_CLASS(REFL_BLACKLIST) BBase
{
    REFL_SER_BODY(BBase)
public:
    BBase() = default;
    virtual ~BBase() = default;

    int m_bbase = 10000;

    void PrintB();
protected:
    int m_protected = 100000;
};

class REFL_SER_CLASS(REFL_WHITELIST) FooA : public FooBase, public BBase
{
    REFL_SER_BODY(FooA)
public:
    REFL_ENABLE FooA(int a, int b);
    virtual ~FooA() = default;

    REFL_SER_ENABLE int m_a = 200000;
    REFL_SER_ENABLE int m_b = 300000;

    REFL_ENABLE void PrintInfo();
    REFL_ENABLE int Add(int a, int b);
    REFL_ENABLE int Add(int a, int b, int c);
    REFL_ENABLE virtual void PrintHelloWorld() const;
};

namespace TestDataNamespace
{

    class REFL_SER_CLASS(REFL_WHITELIST) TestData
    {
        REFL_SER_BODY(TestData)
    public:
        REFL_ENABLE TestData() = default;
        virtual ~TestData() = default;

        REFL_SER_ENABLE float data[100] = {0.0f};

        REFL_ENABLE float GetData(int idx) const;
        REFL_ENABLE void SetData(int idx, float value);
    };

    typedef TestData &TDR;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"

namespace TestDataNamespace
{
    class REFL_SER_CLASS(REFL_WHITELIST) ConstTest
    {
        REFL_SER_BODY(ConstTest)
    public:
        REFL_ENABLE ConstTest() = default;
        virtual ~ConstTest() = default;

        REFL_ENABLE const TestData *m_const_data = nullptr;
        REFL_ENABLE TestData *m_data = nullptr;

        REFL_ENABLE const TestData *GetConstDataPtr() const;
        REFL_ENABLE void SetConstDataPtr(const TestData *data);
        REFL_ENABLE void SetConstDataRef(const TestData &data);
        REFL_ENABLE const TestData &GetConstDataRef() const;
        REFL_ENABLE const TestData *GetTestDataPtrAndAdd();
        REFL_ENABLE TestData *GetTestDataPtr();
        REFL_ENABLE TDR GetTestDataRef();
    };
}

#pragma GCC diagnostic pop

class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest
{
    REFL_SER_BODY(NamespaceTest)
public:
    REFL_ENABLE NamespaceTest() = default;
    virtual ~NamespaceTest() = default;

    REFL_ENABLE void PrintInfo() const;
};

namespace TestHelloWorld
{
    class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest
    {
        REFL_SER_BODY(NamespaceTest)
    public:
        REFL_ENABLE NamespaceTest() = default;
        virtual ~NamespaceTest() = default;

        REFL_ENABLE void PrintInfo() const;
    };

    namespace TestHelloWorld2
    {
        class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest
        {
            REFL_SER_BODY(NamespaceTest)
        public:
            REFL_ENABLE NamespaceTest() = default;
            virtual ~NamespaceTest() = default;

            REFL_ENABLE void PrintInfo() const;
        };
    }
}

#endif // CTEST_REFLECTION_TEST_H
