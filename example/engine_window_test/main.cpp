#include <SDL3/SDL.h>

#include "consts.h"
// #include "EventHandlers/CMainHandler.h"
#include "CMainClassSingle.h"
#include "CSDLWindow.h"

// #include "Controls/CConsoleCtrl.h"
// #include "PythonInterface/PythonInterface.h"
#include "Functional/OptionHandler.h"

//#include "CCtrlTestClass.h"
CMainClassSingle * cmc;

int main(int argc, char * argv[])
{
	freopen("stderr.log", "w", stderr);

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
	SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Python loaded");

	StartupOptions * opt = phraseOptions(argc, argv);
	if (opt->instantQuit)
		return -1;

	cmc = new CMainClassSingle(
			SDL_INIT_VIDEO | SDL_INIT_AUDIO,
			opt->enableVerbose ?
					SDL_LOG_PRIORITY_VERBOSE : SDL_LOG_PRIORITY_INFO);
	cmc->CreateWindow(opt->title.c_str(), opt->resol_x, opt->resol_y);
	cmc->getWindow()->CreateRenderer();

	// console = new CConsoleCtrl(320, opt->resol_y, cmc->getWindow(),
	// { 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE },
	// { 0x00, 0x00, 0xFF, SDL_ALPHA_OPAQUE });
	// cmc->getWindow()->lockFocus(console);

	// board =
	// 		new CGameBoard(cmc->getWindow(), console,
	// 				{ console->getDrawDest().w + LINE_WIDTH * 5, 0, opt->resol_x
	// 						- console->getDrawDest().w - LINE_WIDTH * 10,
	// 						opt->resol_y }, LINE_WIDTH,
	// 				{ 0x00, 0x00, 0xFF, SDL_ALPHA_OPAQUE });
	// console->setAssocGame(board);

	// PythonHelper::setMainGameBoard(board);
	// cmc->getWindow()->showBanner("Chess Engine", 1000);

	// if(opt->startupScript.length() != 0)
	// 	InterpretScript(opt->startupScript, console);

	cmc->MainLoop();

	delete opt;
/*
	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading console");
	delete console;
	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading board");
	delete board;
*/
	SDL_LogVerbose(SDL_LOG_CATEGORY_APPLICATION, "Unloading Main-class");
	delete cmc;
	// Py_Finalize();
	return 0;
}
