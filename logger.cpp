
#include "logger.h"
#include <iostream>
#include <stdarg.h>
#include <string>

using namespace std;

void LogConsole::LogDebug(const char* tag, int num, ...)
{
	char formatBuf[128] = {};
	sprintf(formatBuf, "[%s %d %s() : (%s)] ", __FILE__, __LINE__, __FUNCTION__, tag);
	std::string priStr = formatBuf;
	char* str = nullptr;
	va_list valist;
	va_start(valist, num);
	for (int i = 0; i < num; i++)
	{
		str = va_arg(valist, char*);
		priStr = priStr + str + "  ";
	}
	va_end(valist);
	std::cout << priStr << std::endl;
	return;
}

void LogConsole::WinConsolePrint(const char* str)
{
#ifdef WIN32
	SetConsoleOutputCP(CP_UTF8);
	// german chars won't appear
	//  char const* text = "abcdefghijklmnopqrstuvwxyz";
	//  int len = MultiByteToWideChar(CP_UTF8, 0, text, -1, 0, 0);

	char formatBuf[2048] = {};
	sprintf(formatBuf, "[%s %d %s() : %s] ", __FILE__, __LINE__, __FUNCTION__, str);
	int len = MultiByteToWideChar(CP_UTF8, 0, formatBuf, -1, 0, 0);
	wchar_t* unicode_text = new wchar_t[len];
	// MultiByteToWideChar(CP_UTF8, 0, text, -1, unicode_text, len);
	MultiByteToWideChar(CP_UTF8, 0, formatBuf, -1, unicode_text, len);
	wprintf(L"%s", unicode_text);
#endif
}
