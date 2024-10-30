#include "Reflection/reflection.h"
#include "Framework/go/GameObject.h"

class REFLECTION TestGO: public Engine::GameObject
{

};

class REFLECTION FooBase
{
public:
    REFLECTION FooBase() = default;
    virtual ~FooBase() = default;

    REFLECTION int m_foobase = 1000;

    REFLECTION virtual void PrintHelloWorld();
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
    REFLECTION virtual void PrintHelloWorld();
};
