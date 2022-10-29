#ifndef _UTIL_H
#define _UTIL_H

#include <stdio.h>

#define ERROR_LOG(...) fprintf(stderr, "[ERROR] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n");

#ifdef NDEBUG
	#define DEBUG_LOG
#else
	#define DEBUG_LOG(...) printf("[DEBUG] "); printf(__VA_ARGS__); printf("\n")
#endif

#if defined(__GNUC__)
	#define PACK( data ) data __attribute((__packed__))

	#include <byteswap.h>
	#define bswap_16 bswap_16
	#define bswap_32 bswap_32
	#define bswap_64 bswap_64
#elif defined(_MSC_VER)
	#define PACK( data ) __pragma(pack(push, 1)) data __pragma(pack(pop))

	#include <stdlib.h>
	#define bswap_16 _byteswap_ushort
	#define bswap_32 _byteswap_ulong
	#define bswap_64 _byteswap_uint64
#endif



#endif // _UTIL_H