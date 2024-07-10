#ifndef CONSTS_H_INCLUDED
#define CONSTS_H_INCLUDED

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define RMASK (0xff000000)
#define GMASK (0x00ff0000)
#define BMASK (0x0000ff00)
#define AMASK (0x000000ff)
#else
#define RMASK (0x000000ff)
#define GMASK (0x0000ff00)
#define BMASK (0x00ff0000)
#define AMASK (0xff000000)
#endif

#define InRGBRange(x) (x >= 0 && x < 256)

#define MAX_CMD_HISTORY (50)
#define PROMOTE_SIZE (20)           // Size of the promote line, in pixels
#define LINE_WIDTH (2)              // Width of a line, in pixels
#define MAX_CHAR_PER_CMD (50)

#define FPS_LIMIT (30)
#define TPF_LIMIT (1000 / FPS_LIMIT)

#define STR_CONFIGFILE ".packageConfig"
#define STR_INITFILE ".packageInit"
#define STR_MAINPYTHONFILE "packageMain"

const int MAX_PATH_LENG = 512;
const int MAX_MSGSTR_LEN = 512;

const char STR_PY_STARTUP[] =
		R"DIM(
import sys;
import os;
sys.path.append('./');
import redirectOutput as redirOut;
redirOut.redirectStdoutStderr();
)DIM";

#define _T(x) L ## x

#define ERRORMSG_PY_NOTINIT "Python is not initialized"
#define LERRORMSG_PY_NOTINIT L"Python is not initialized"

#define ERRORMSG_PY_HEADLINE "\n **********Python Error Message**********\n"
#define LERRORMSG_PY_HEADLINE L"\n **********Python Error Message**********\n"

#define COLOR_FG_DEFAULT { 0x00, 0x00, 0xFF, SDL_ALPHA_OPAQUE }
#define COLOR_BG_DEFAULT { 0xFF, 0xFF, 0xFF, SDL_ALPHA_TRANSPARENT}

#define PYCMD_SWITCHDIR_STRING(x) "os.chdir(\".\\" + x + "\")"
#define PYCMD_SWITCHDIR_CSTR(x) "os.chdir(\".\\" #x "\")"

#endif // CONSTS_H_INCLUDED
