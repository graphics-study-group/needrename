#ifndef CEVENTHANDLER_H
#define CEVENTHANDLER_H

#include <SDL3/SDL.h>

#include "CSDLWindow.h"

class CEventHandler
{
public:
    CEventHandler(CSDLWindow * wnd);
    virtual ~CEventHandler();

    virtual bool anteEventLoop(CSDLWindow *) = 0;
    virtual bool postEventLoop(CSDLWindow *) = 0;
    virtual bool onEvent(CSDLWindow *, const SDL_Event &) = 0;

protected:

    CSDLWindow * const parent;

private:
};

#endif // CEVENTHANDLER_H
