#ifndef _UTIL_H
#define _UTIL_H

#include <stdio.h>

#ifdef NDEBUG
#define DEBUG_LOG
#else
#define DEBUG_LOG(msg, ...) printf("[DEBUG] " ##msg "\n", __VA_ARGS__);
#endif

#endif // _UTIL_H