#ifndef OPTIONHANDLER_H_INCLUDED
#define OPTIONHANDLER_H_INCLUDED

#include <getopt.h>
#include <cstdlib>
#include <string>

struct StartupOptions
{
    int resol_x {1024}, resol_y {768};
    int fntSz {12};
    bool enableVerbose {false};

    std::string title{};
    std::string fntName{};
    std::string graphics_api{};
    std::string startupScript{};

    bool instantQuit {false};
};

namespace OptionDeclaration
{

    extern const char help_text[];
    enum extra_options
    {
        OPT_SETTITLE = 257,
        OPT_SETFONT,
        OPT_SETSIZE,
        OPT_STARTUP,
    };
    extern const char *short_options;
    extern const option long_options[];
}

/// @note atoi() is used
StartupOptions *ParseOptions(int argc, char **argv);

#endif // OPTIONHANDLER_H_INCLUDED
