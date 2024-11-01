#ifndef CTEST_REFLECTION_REGISTERATION_TEST_H
#define CTEST_REFLECTION_REGISTERATION_TEST_H

#include "Reflection/reflection.h"

class REFLECTION FooBase
{
public:
    REFLECTION FooBase() = default;
    virtual ~FooBase() = default;

    REFLECTION int m_foobase = 1000;

    REFLECTION virtual void PrintHelloWorld() const;
};

class REFLECTION BBase
{
public:
    REFLECTION BBase() = default;
    virtual ~BBase() = default;

    REFLECTION int m_bbase = 10000;

    REFLECTION void PrintB();
};

class REFLECTION FooA : public FooBase, public BBase
{
public:
    REFLECTION FooA(int a, int b) : m_a(a), m_b(b) {}
    virtual ~FooA() = default;

    REFLECTION int m_a = 200000;
    REFLECTION int m_b = 300000;

    REFLECTION void PrintInfo();
    REFLECTION int Add(int a, int b);
    REFLECTION int Add(int a, int b, int c);
    REFLECTION virtual void PrintHelloWorld() const;
};

class REFLECTION TestData
{
public:
    REFLECTION TestData() = default;
    virtual ~TestData() = default;

    REFLECTION float data[100] = {0.0f};
};

typedef TestData &TDR;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Weffc++"

class REFLECTION ConstTest
{
public:
    REFLECTION ConstTest() = default;
    virtual ~ConstTest() = default;

    REFLECTION const TestData *m_const_data = nullptr;
    REFLECTION TestData *m_data = nullptr;

    REFLECTION const TestData *GetConstDataPtr() const;
    REFLECTION void SetConstDataPtr(const TestData *data);
    REFLECTION void SetConstDataRef(const TestData &data);
    // REFLECTION const TestData &GetConstDataRef() const;
    REFLECTION const TestData *GetTestDataPtrAndAdd();

    REFLECTION TestData *GetTestDataPtr();
    REFLECTION TDR GetTestDataRef();
};

#pragma GCC diagnostic pop

class REFLECTION NamespaceTest
{
public:
    REFLECTION NamespaceTest() = default;
    virtual ~NamespaceTest() = default;

    REFLECTION void PrintInfo() const;
};

namespace TestHalloWorld
{
    class REFLECTION NamespaceTest
    {
    public:
        REFLECTION NamespaceTest() = default;
        virtual ~NamespaceTest() = default;

        REFLECTION void PrintInfo() const;
    };

    namespace TestHalloWorld2
    {
        class REFLECTION NamespaceTest
        {
        public:
            REFLECTION NamespaceTest() = default;
            virtual ~NamespaceTest() = default;

            REFLECTION void PrintInfo() const;
        };
    }
}

#endif // CTEST_REFLECTION_REGISTERATION_TEST_H
