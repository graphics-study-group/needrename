#ifndef OPTIONHANDLER_H_INCLUDED
#define OPTIONHANDLER_H_INCLUDED

#include "../core_export.h"
#include <memory>
#include <string>

struct StartupOptions {
    int resol_x{1024}, resol_y{768};
    int fntSz{12};
    bool enableVerbose{false};
    bool headless{false};

    std::string title{};
    std::string fntName{};
    std::string graphics_api{};
    std::string startupScript{};

    bool instantQuit{false};
};

/**
 * @brief Parses command-line options into a StartupOptions object.
 *
 * Supports the long options --help, --resolutionX, --resolutionY, --verbose,
 * --title, --fontFile, --fontSize, --startup, and --headless (in both
 * --opt=value and --opt value forms), plus the short options -x, -y, -v,
 * and -?. Unknown options and --help / -? show help and set instantQuit.
 *
 * @param argc Argument count from main
 * @param argv Argument vector from main
 * @return Parsed options; never null
 */
CORE_API std::unique_ptr<StartupOptions> ParseOptions(int argc, char **argv);

#endif // OPTIONHANDLER_H_INCLUDED
