#ifndef TEMPLATEMOVEDISABLED_H
#define TEMPLATEMOVEDISABLED_H


class TemplateMoveDisabled
{
public:
    TemplateMoveDisabled();
    virtual ~TemplateMoveDisabled() = 0;

    TemplateMoveDisabled(TemplateMoveDisabled &&) = delete;
    operator =(TemplateMoveDisabled &&) = delete;

protected:

private:
};

#endif // TEMPLATEMOVEDISABLED_H
