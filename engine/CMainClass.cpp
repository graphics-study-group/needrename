#include <CMainClass.h>
#include <CException/exception.h>

#include <SDL3/SDL.h>

CMainClass::CMainClass(Uint32 flags, SDL_LogPriority LogPriority)
{
    //ctor
    if(SDL_Init(flags) < 0)
        throw Exception::SDLExceptions::cant_init();

    SDL_SetLogPriorities(LogPriority);
}

CMainClass::~CMainClass()
{
    //dtor
    SDL_Quit();
}


