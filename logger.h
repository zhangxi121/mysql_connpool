#ifndef LOGGER_H
#define LOGGER_H

#if defined(__unix__)
#include <unistd.h>
#elif defined(WIN32)
#include <windows.h>
#endif

#include <stdio.h>

namespace LogConsole
{
    void LogDebug(const char *tag, int num, ...);
    void WinConsolePrint(const char *str);
}

#define CONSOLE_LOG_DBG(fmt, ...)                                                          \
    do                                                                                     \
    {                                                                                      \
        printf("[%s:%d:%s()] " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

#define FILE_LOG_DBG(FILE_PTR, fmt, ...)                                                              \
    do                                                                                                \
    {                                                                                                 \
        fprintf(FILE_PTR, "[%s:%d:%s()] " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
    } while (0)

extern FILE *g_pfile;

//
//
//
//

#endif // LOGGER_H
