#include "CMainClassSingle.h"
#include <exception>

CMainClassSingle::CMainClassSingle(Uint32 flags, SDL_LogPriority logPrior) :
    CMainClass(flags, logPrior)
{
//    this->handler = nullptr;
    this->window = nullptr;
}

CMainClassSingle::~CMainClassSingle()
{
    if(this->window != nullptr)
        delete this->window;
//    if(this->handler != nullptr)
//        delete this->handler;
}

CSDLWindow * CMainClassSingle::getWindow()
{
    return this->window;
}

void CMainClassSingle::CreateWindow(const char * title, int w, int h, Uint32 flags)
{
    if(this->window != nullptr)
        return;
    this->window = new CSDLWindow(title, w, h, flags);
}

void CMainClassSingle::MainLoop()
{
//    if(this->handler == nullptr)
//        throw Exception::mainframeExceptions::handler_not_specified();

    SDL_Event event;
    bool onQuit = false;

    unsigned int FPS_TIMER = 0;

    while(!onQuit)
    {
    	try
    	{
        if(!this->window->anteEventLoop())
        {
            onQuit = true;
            SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Exited after anteEventLoop()");
        }


        while(SDL_PollEvent(&event))
        {
            if(event.type == SDL_EVENT_QUIT)
            {
                onQuit = true;
                break;
            }
            if(!this->window->dispatchEvents(event))
            {
                onQuit = true;
                SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Exited after onEvent()");
            }
        }

        if(!this->window->postEventLoop())
        {
            onQuit = true;
            SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Exited after postEventLoop()");
        }

        if(SDL_GetTicks() - FPS_TIMER < TPF_LIMIT)
            SDL_Delay(TPF_LIMIT - SDL_GetTicks() + FPS_TIMER);
        FPS_TIMER = SDL_GetTicks();

        //std::this_thread::sleep_for(std::chrono::milliseconds(10));
    	}
    	catch(std::exception & except)
    	{
    		fprintf(stderr, "%s", except.what());
    		// Do a forced redraw to print error messages
    		this->window->onDrawOverall(true);
    		throw;
    	}
    }
    SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "The main loop is ended.");
}
