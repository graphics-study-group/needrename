#include <SDL3/SDL.h>

#include "consts.h"
#include "MainClass.h"
#include "Functional/SDLWindow.h"

Engine::MainClass * cmc;

int main(int argc, char * argv[])
{
	SDL_Init(SDL_INIT_VIDEO);

	StartupOptions * opt = phraseOptions(argc, argv);
	if (opt->instantQuit)
		return -1;

	cmc = new Engine::MainClass(
			SDL_INIT_VIDEO,
			opt->enableVerbose ? SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);
	cmc->Initialize(opt);
	cmc->MainLoop();

	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading StartupOptions");
	delete opt;
	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
	delete cmc;

	return 0;
}
