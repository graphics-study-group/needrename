#ifndef CSDLLOGGER_H
#define CSDLLOGGER_H

#include <SDL3/SDL.h>

class CSDLLogger
{
public:
    CSDLLogger() = delete;
    CSDLLogger(SDL_LogPriority);
    virtual ~CSDLLogger();

    CSDLLogger(const CSDLLogger &) = delete;
    operator = (const CSDLLogger &) = delete;

protected:

private:
};

#endif // CSDLLOGGER_H
