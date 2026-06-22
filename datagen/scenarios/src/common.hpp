#ifndef COMMON_HPP
#define COMMON_HPP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdbool.h>
#include <time.h>

/* Information Log. */
#define LOG_RED    "\033[0;31m"        /* 0 -> normal ;  31 -> red */
#define LOG_CYAN   "\033[0;36m"        /* 1 -> bold ;  36 -> cyan */
#define LOG_GREEN  "\033[0;32m"        /* 4 -> underline ;  32 -> green */
#define LOG_BLUE   "\033[0;34m"        /* 9 -> strike ;  34 -> blue */
#define LOG_BLACK   "\033[0;30m"
#define LOG_BROWN   "\033[0;33m"
#define LOG_MAGENTA "\033[0;35m"
#define LOG_GRAY    "\033[0;37m"
#define LOG_NONE    "\033[0m"        /* to flush the previous property */

#define LOG_INFO(format, ...) fprintf(stderr, format, ##__VA_ARGS__) 
#define LOG_ERROR(format, ...) {					                             \
		fprintf(stderr, LOG_RED "ERROR: " LOG_NONE format "\n", ##__VA_ARGS__);	 \
		exit(1);						                                         \
	}
#ifdef DEBUG
#define LOG_DEBUG(format, ...) LOG_INFO(format, ##__VA_ARGS__)
#else
#define LOG_DEBUG(format, ...) {}
#endif

/* Files Operations. */
#define OPEN_FILE_OPT(file, path, opt)				                       \
		FILE* file = fopen(std::string(path).c_str(), opt);		           \
		if (!file) LOG_ERROR("Can not open file: %s with opt %s\n", \
			     std::string(path).c_str(), std::string(opt).c_str());
#define OPEN_FILE(file, path) OPEN_FILE_OPT(file, path, "r")
#define CLOSE_FILE(file) fclose(file);

#endif