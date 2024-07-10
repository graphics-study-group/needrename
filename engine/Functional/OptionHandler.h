#ifndef OPTIONHANDLER_H_INCLUDED
#define OPTIONHANDLER_H_INCLUDED

#include <getopt.h>
#include <cstdlib>
#include <string>

struct StartupOptions
{
    int resol_x, resol_y;
    int fntSz;
    bool enableVerbose;

    std::string title;
    std::string fntName;
    std::string startupScript;

    bool instantQuit;

    StartupOptions()
    {
        resol_x = 1024, resol_y = 768;
        fntSz = 12;
        enableVerbose = false;

        title = "Chess Engine";
        fntName = "ZfullGB.ttf";
        startupScript = "";

        instantQuit = false;
    }
};

namespace OptionDeclaration
{

const char help_text[] = R"DIM(programName
    -? -v
    --resolutionX=X
    --resolutionY=Y
    --verbose
    --title=TITLE
    --fontFile=FILENAME
    --fontSize=SIZE
	--startup=SCRIPT)DIM";
enum extra_options
{
    OPT_SETTITLE = 257,
    OPT_SETFONT,
    OPT_SETSIZE,
	OPT_STARTUP,
};
const char * short_options = "?x:y:v";
const option long_options[] =
{
    {"help", no_argument, NULL, '?'},
    {"resolutionX", required_argument, NULL, 'x'},
    {"resolutionY", required_argument, NULL, 'y'},
    {"verbose", no_argument, NULL, 'v'},
    {"title", required_argument, NULL, OPT_SETTITLE},
    {"fontFile", required_argument, NULL, OPT_SETFONT},
    {"fontSize", required_argument, NULL, OPT_SETSIZE},
    {"startup", required_argument, NULL, OPT_STARTUP}
};
}


/// @note atoi() is used
StartupOptions * phraseOptions(int argc, char ** argv)
{
    StartupOptions * opts = new StartupOptions;
    int opt = 0;

    while((opt =
                getopt_long(argc, argv,
                            OptionDeclaration::short_options,
                            OptionDeclaration::long_options,
                            NULL)) != -1)
    {
        switch(opt)
        {
        case '?':
            opts->instantQuit = true;
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Help", OptionDeclaration::help_text, NULL);
            fprintf(stderr, OptionDeclaration::help_text);
            return opts;
        case 'x':
            opts->resol_x = atoi(optarg);
            break;
        case 'y':
            opts->resol_y = atoi(optarg);
            break;
        case 'v':
            opts->enableVerbose = true;
            break;
        case OptionDeclaration::OPT_SETTITLE:
            opts->title = optarg;
            break;
        case OptionDeclaration::OPT_SETFONT:
            opts->fntName = optarg;
            break;
        case OptionDeclaration::OPT_SETSIZE:
            opts->fntSz = atoi(optarg);
            break;
        case OptionDeclaration::OPT_STARTUP:
        	opts->startupScript = optarg;
        	break;
        }
    }

    return opts;
}

#endif // OPTIONHANDLER_H_INCLUDED
