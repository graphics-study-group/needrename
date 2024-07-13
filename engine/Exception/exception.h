#ifndef EXCEPTION_H_INCLUDED
#define EXCEPTION_H_INCLUDED

#include <SDL3/SDL.h>
// #include <Python.h>

#include "consts.h"
#include <string>
#include <exception>
#include <cstdio>

namespace Exception
{

class ExceptionWithMsgbox: public std::exception
{
public:
};

class SDLException: public std::exception
{
public:
	virtual const char * what() const noexcept
	{
		SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
		return "An error has been encountered when using SDL.\n";
	}

	virtual ~SDLException()
	{
	}
};

class mainframeException: public std::exception
{
public:
	virtual const char * what() const noexcept
	{
		return "An internal error has occurred.\n";
	}
};

class embeddedPythonException: public std::exception
{

public:
	// TODO: python exception
	// embeddedPythonException(bool showMessageBox = true)
	// {

	// 	//std::wstring wstrErrorMsg;
	// 	std::string strErrorMsg;

	// 	if (!Py_IsInitialized())
	// 	{
	// 		//wstrErrorMsg = LERRORMSG_PY_NOTINIT;
	// 		strErrorMsg = ERRORMSG_PY_NOTINIT;
	// 		return;
	// 	}

	// 	if (PyErr_Occurred() != NULL)
	// 	{
	// 		PyObject *type_obj, *value_obj, *traceback_obj;
	// 		PyErr_Fetch(&type_obj, &value_obj, &traceback_obj);
	// 		if (value_obj == NULL)
	// 			return;

	// 		//wstrErrorMsg = LERRORMSG_PY_HEADLINE;
	// 		strErrorMsg = ERRORMSG_PY_HEADLINE;

	// 		PyErr_NormalizeException(&type_obj, &value_obj, 0);
	// 		if (PyUnicode_Check(PyObject_Str(value_obj)))
	// 		{
	// 			//wstrErrorMsg += PyUnicode_AsUnicode(PyObject_Str(value_obj));
	// 			strErrorMsg += PyUnicode_AsUTF8(PyObject_Str(value_obj));
	// 		}

	// 		if (traceback_obj != NULL)
	// 		{
	// 			//wstrErrorMsg += L"\nTraceback:\n";

	// 			PyObject * pModuleName = PyUnicode_FromString("traceback");
	// 			PyObject * pTraceModule = PyImport_Import(pModuleName);
	// 			Py_XDECREF(pModuleName);
	// 			if (pTraceModule != NULL)
	// 			{
	// 				PyObject * pModuleDict = PyModule_GetDict(pTraceModule);
	// 				if (pModuleDict != NULL)
	// 				{
	// 					PyObject * pFunc = PyDict_GetItemString(pModuleDict,
	// 							"format_exception");
	// 					if (pFunc != NULL)
	// 					{
	// 						PyObject * errList = PyObject_CallFunctionObjArgs(
	// 								pFunc, type_obj, value_obj, traceback_obj,
	// 								NULL);
	// 						if (errList != NULL)
	// 						{
	// 							int listSize = PyList_Size(errList);
	// 							for (int i = 0; i < listSize; ++i)
	// 							{
	// 								//wstrErrorMsg += PyUnicode_AsUnicode(
	// 								//		PyList_GetItem(errList, i));
	// 								strErrorMsg += PyUnicode_AsUTF8(
	// 										PyList_GetItem(errList, i));
	// 							}
	// 						}
	// 					}
	// 				}
	// 				Py_XDECREF(pTraceModule);
	// 			}
	// 		}
	// 		Py_XDECREF(type_obj);
	// 		Py_XDECREF(value_obj);
	// 		Py_XDECREF(traceback_obj);
	// 	}

	// 	//wstrErrorMsg += L"\n";

	// 	//SDL_LogError(SDL_LOG_CATEGORY_ERROR, strErrorMsg.c_str());
	// 	fprintf(stderr, strErrorMsg.c_str());
	// 	if(showMessageBox)
	// 		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error", strErrorMsg.c_str(), NULL);
	// 	fflush(stderr);
	// }

	virtual const char * what() const noexcept
	{
		return "An error has occurred when interacting with embedded Python interpreter.\n";
	}
};

namespace SDLExceptions
{
}
;
namespace mainframeExceptions
{
}
;
namespace embeddedPythonExceptions
{
}
;

}

#include "SDL_exception.h"
#include "mainframe_exception.h"
#include "embPyn_exception.h"

#endif // EXCEPTION_H_INCLUDED
