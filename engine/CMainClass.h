#ifndef CMAINCLASS_H
#define CMAINCLASS_H

#include <SDL3/SDL.h>
#include <list>

// #include "Wrappers/CTTFWrapper.h"
#include "ClassTemplate/TemplateMoveDisabled.h"
#include "ClassTemplate/TemplateCopyDisabled.h"

/// The main class of an application
/// This class initializes SDL and logging
class CMainClass : public TemplateCopyDisabled, public TemplateMoveDisabled
{
public:
    CMainClass() = delete;

    CMainClass(Uint32, SDL_LogPriority = SDL_LOG_PRIORITY_INFO);
    virtual ~CMainClass();

    /// Enter the main loop of the application
    /// @note Call MainLoop() after everything has been completed
    virtual void MainLoop() = 0;

protected:

private:
};

#endif // CMAINCLASS_H
