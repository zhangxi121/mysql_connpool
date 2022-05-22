#ifndef COMMON_H
#define COMMON_H

//
// win32  预处理器添加宏  _CRT_SECURE_NO_DEPRECATE
//

#include "logger.h"

// #define LOG(str)
// #define LOG(str)   std::cout << __FILE__ << ":" << __LINE__ << " " << __TIMESTAMP__ << " : " << str  <<  std::endl;
#define LOG(fmt, ...) CONSOLE_LOG_DBG(fmt, ##__VA_ARGS__) // 这里选用控制台打印来测试   increase thread  和  decrease thread, 原因如下所述,
// #define LOG(fmt, ...)    FILE_LOG_DBG(g_pfile, fmt, ##__VA_ARGS__)  // g_pfile 当多线程时候,写入缓冲区以后,没有即时 flush() , 当进程关闭, 导致 1234.txt 没有日志记录,
//




#endif // COMMON_H
