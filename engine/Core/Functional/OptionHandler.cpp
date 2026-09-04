#include "Core/Functional/OptionHandler.h"
#include <SDL3/SDL.h>

#include <charconv>
#include <cstdio>
#include <string_view>
#include <system_error>

namespace {
    const char help_text[] = R"DIM(%s
        -? -v
        --resolutionX=X
        --resolutionY=Y
        --verbose
        --title=TITLE
        --fontFile=FILENAME
        --fontSize=SIZE
        --startup=SCRIPT
        --headless)DIM";

    bool ParseInt(std::string_view str, int &out) {
        const char *begin = str.data();
        const char *end = begin + str.size();
        auto [ptr, ec] = std::from_chars(begin, end, out);
        return ec == std::errc{} && ptr == end;
    }
} // namespace

std::unique_ptr<StartupOptions> ParseOptions(int argc, char **argv) {
    auto opts = std::make_unique<StartupOptions>();
    const char *program_name = (argc > 0 && argv[0]) ? argv[0] : "programName";

    auto show_help = [&] {
        char formatted[1024];
        std::snprintf(formatted, sizeof(formatted), help_text, program_name);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Help", formatted, nullptr);
        std::fprintf(stderr, "%s", formatted);
        opts->instantQuit = true;
    };

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];

        if (arg.starts_with("--")) {
            std::string_view name = arg.substr(2);
            std::string_view value{};
            const size_t eq = name.find('=');
            const bool has_value = eq != std::string_view::npos;
            if (has_value) {
                value = name.substr(eq + 1);
                name = name.substr(0, eq);
            }

            if (name == "help") {
                show_help();
                return opts;
            }
            if (name == "verbose") {
                opts->enableVerbose = true;
                continue;
            }
            if (name == "headless") {
                opts->headless = true;
                continue;
            }

            const bool is_value_opt = name == "resolutionX" || name == "resolutionY" || name == "title"
                                      || name == "fontFile" || name == "fontSize" || name == "startup";
            if (!is_value_opt) {
                show_help();
                return opts;
            }
            if (!has_value) {
                if (i + 1 >= argc) {
                    show_help();
                    return opts;
                }
                value = argv[++i];
            }

            if (name == "resolutionX") {
                if (!ParseInt(value, opts->resol_x)) continue;
            } else if (name == "resolutionY") {
                if (!ParseInt(value, opts->resol_y)) continue;
            } else if (name == "fontSize") {
                if (!ParseInt(value, opts->fntSz)) continue;
            } else if (name == "title") {
                opts->title = value;
            } else if (name == "fontFile") {
                opts->fntName = value;
            } else {
                opts->startupScript = value;
            }
            continue;
        }

        if (arg.starts_with("-") && arg.size() > 1) {
            const char opt_char = arg[1];
            switch (opt_char) {
            case '?':
                show_help();
                return opts;
            case 'v':
                opts->enableVerbose = true;
                break;
            case 'x':
            case 'y': {
                if (i + 1 >= argc) {
                    show_help();
                    return opts;
                }
                int parsed = 0;
                if (!ParseInt(argv[++i], parsed)) break;
                (opt_char == 'x' ? opts->resol_x : opts->resol_y) = parsed;
                break;
            }
            default:
                show_help();
                return opts;
            }
        }
    }

    return opts;
}
