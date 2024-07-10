#ifndef SINGLETON_H_INCLUDED
#define SINGLETON_H_INCLUDED

class Singleton_base
{
public:
    Singleton_base(){};
    virtual ~Singleton_base() = 0;
    // Disable copy/move constructor
    Singleton_base(const Singleton_base &) = delete;
    Singleton_base(Singleton_base &&) = delete;
    // Disable copy/move operator
    operator = (const Singleton_base &) = delete;
    operator = (Singleton_base &&) = delete;
};

#endif // SINGLETON_H_INCLUDED
