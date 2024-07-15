/*
 * embPyn_exception.h
 *
 *  Created on: 2019??2??9??
 *      Author: Administrator
 */

#ifndef CEXCEPTION_EMBPYN_EXCEPTION_H_
#define CEXCEPTION_EMBPYN_EXCEPTION_H_

#include <cstring>

namespace Exception
::embeddedPythonExceptions
{
	class cant_init_python : public embeddedPythonException
	{
	public:
		const char * what() const noexcept
		{
			return "Cannot initialize Embedded Python Interpreter.";
		}
	};

	class cant_import_module : public embeddedPythonException
	{
		char whatMsg[256];
	public:

		cant_import_module() = delete;
		cant_import_module(const char * moduleName)
		{
			strcat(whatMsg, "Cannot import module:\"");
			strcat(whatMsg, moduleName);
			strcat(whatMsg, "\"");
		}

		const char * what() const noexcept
		{
			return whatMsg;
		}
	};

	class cant_retrieve_attr : public embeddedPythonException
	{
		char whatMsg[256];
	public:

		cant_retrieve_attr() = delete;
		cant_retrieve_attr(const char * attrName)
		{
			strcat(whatMsg, "Cannot retrieve attribute:\"");
			strcat(whatMsg, attrName);
			strcat(whatMsg, "\"");
		}

		const char * what() const noexcept
		{
			return whatMsg;
		}
	};

	class type_error : public embeddedPythonException
	{
	public:
		const char * what() const noexcept
		{
			return "Cannot covert a PyObject into any appropriate C/C++ object";
		}
	};
}

#endif /* CEXCEPTION_EMBPYN_EXCEPTION_H_ */
