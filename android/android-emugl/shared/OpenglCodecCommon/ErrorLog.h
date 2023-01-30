/*
* Copyright (C) 2011 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#ifndef _ERROR_LOG_H_
#define _ERROR_LOG_H_

#include <time.h>
#include <stdio.h>

#ifdef REMOTE_RENDER
#define SKIP_FLUSH 0
#else
#define SKIP_FLUSH 1
#endif

#define EMUGL_DEBUG

#ifdef FATAL
#undef FATAL
#endif
#ifdef ERR
#undef ERR
#endif
#ifdef WARN
#undef WARN
#endif
#ifdef INFO
#undef INFO
#endif
#ifdef DBG
#undef DBG
#endif

#ifdef __ANDROID__
#include <android/log.h>
#define TAG "EmuGLRender"
#define FATAL(...)    __android_log_print(ANDROID_LOG_FATAL, TAG, __VA_ARGS__)
#define ERR(...)    __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define WARN(...)    __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define INFO(...)    __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define DBG(...)    __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#else
#include <ctime>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>

static void LogHead(char level, const char* tag)
{
    struct timeval tv;
    struct timezone tz;
    struct tm now;
    (void)gettimeofday(&tv, &tz);
    (void)localtime_r(&tv.tv_sec, &now);

    (void)fprintf(stdout, "%02d-%02d %02d:%02d:%02d.%03ld\t%d\t%d\t%c EmuGL-%s\t",
        ++now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, tv.tv_usec / 1000,
        getpid(), syscall(SYS_gettid), level, tag);
}

#define FATAL(...)    LogHead('F', __FUNCTION__); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n");
#define ERR(...)    LogHead('E', __FUNCTION__); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n");
#define WARN(...)    LogHead('W', __FUNCTION__); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n");
#define INFO(...)    LogHead('I', __FUNCTION__); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n");
#ifdef EMUGL_DEBUG
#    define DBG(...)    LogHead('D', __FUNCTION__); fprintf(stdout, __VA_ARGS__); fprintf(stdout, "\n");
#else
#    define DBG(...)    ((void)0)
#endif
#endif
#endif  // _ERROR_LOG_H_
