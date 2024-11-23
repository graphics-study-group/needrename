#ifndef CTEST_REFLECTION_TEST_H
#define CTEST_REFLECTION_TEST_H

#include <meta_reflection_test/reflection.hpp>

class REFL_SER_CLASS(REFL_BLACKLIST) FooBase
{
    REFL_SER_BODY()
public:
    FooBase() = default;
    virtual ~FooBase() = default;

    int m_foobase = 1000;

    virtual void PrintHelloWorld() const;
};

class REFL_SER_CLASS(REFL_BLACKLIST) BBase
{
    REFL_SER_BODY()
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
    REFL_SER_BODY()
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
        REFL_SER_BODY()
    public:
        REFL_ENABLE TestData() = default;
        virtual ~TestData() = default;

        REFL_SER_ENABLE float data[100] = {0.0f};
    };

    typedef TestData &TDR;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"

namespace TestDataNamespace
{
    class REFL_SER_CLASS(REFL_WHITELIST) ConstTest
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE ConstTest() = default;
        virtual ~ConstTest() = default;

        REFL_ENABLE const TestData *m_const_data = nullptr;
        REFL_SER_ENABLE TestData *m_data = nullptr;

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
    REFL_SER_BODY()
public:
    REFL_ENABLE NamespaceTest() = default;
    virtual ~NamespaceTest() = default;

    REFL_ENABLE void PrintInfo() const;
};

namespace TestHalloWorld
{
    class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest
    {
        REFL_SER_BODY()
    public:
        REFL_ENABLE NamespaceTest() = default;
        virtual ~NamespaceTest() = default;

        REFL_ENABLE void PrintInfo() const;
    };

    namespace TestHalloWorld2
    {
        class REFL_SER_CLASS(REFL_WHITELIST) NamespaceTest
        {
            REFL_SER_BODY()
        public:
            REFL_ENABLE NamespaceTest() = default;
            virtual ~NamespaceTest() = default;

            REFL_ENABLE void PrintInfo() const;
        };
    }
}

#endif // CTEST_REFLECTION_TEST_H
