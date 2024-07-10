#ifndef CMAINCLASSSINGLE_H
#define CMAINCLASSSINGLE_H

#include <thread>
#include <chrono>

#include "CException/exception.h"

#include "consts.h"
#include "CMainClass.h"
#include "CSDLWindow.h"
#include "CEventHandler.h"

class CMainClassSingle : public CMainClass
{
public:
    CMainClassSingle(Uint32, SDL_LogPriority = SDL_LOG_PRIORITY_INFO);
    virtual ~CMainClassSingle();

    /// Get the window
    CSDLWindow * getWindow();

    /// Create a new window using given parameters
    void CreateWindow(const char *, int, int, Uint32 = SDL_WINDOW_OPENGL);

    //virtual bool postEventLoop(){return true;};
    //virtual bool anteEventLoop(){return true;};

    //virtual bool onEvent(SDL_Event *){return true;};

    /// The main loop of this class
    void MainLoop();

// TODO (hp#1#): Handler is depreciated
/*
    template <typename T, class ... Args>
    void emplaceHandler(Args ... args)
    {
        if(this->handler != nullptr)
            delete this->handler;
        this->handler = new (T)(args...);
    }

    CEventHandler * getHandler();
*/

protected:

    CSDLWindow * window;
    //CEventHandler * handler;

private:
};

#endif // CMAINCLASSSINGLE_H
