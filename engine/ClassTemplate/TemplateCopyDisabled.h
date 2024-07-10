#ifndef TEMPLATECOPYDISABLED_H
#define TEMPLATECOPYDISABLED_H


class TemplateCopyDisabled
{
public:
    TemplateCopyDisabled();
    virtual ~TemplateCopyDisabled() = 0;

    TemplateCopyDisabled(const TemplateCopyDisabled &) = delete;
    operator =(const TemplateCopyDisabled &) = delete;

protected:

private:
};

#endif // TEMPLATECOPYDISABLED_H
