/*********************************************************************
 *
 * $Id: ythread.h 63696 2024-12-13 17:44:50Z mvuilleu $
 *
 * OS-independent thread and synchronization library
 *
 * - - - - - - - - - License information: - - - - - - - - - 
 *
 *  Copyright (C) 2011 and beyond by Yoctopuce Sarl, Switzerland.
 *
 *  Yoctopuce Sarl (hereafter Licensor) grants to you a perpetual
 *  non-exclusive license to use, modify, copy and integrate this
 *  file into your software for the sole purpose of interfacing 
 *  with Yoctopuce products. 
 *
 *  You may reproduce and distribute copies of this file in 
 *  source or object form, as long as the sole purpose of this
 *  code is to interface with Yoctopuce products. You must retain 
 *  this notice in the distributed source file.
 *
 *  You should refer to Yoctopuce General Terms and Conditions
 *  for additional information regarding your rights and 
 *  obligations.
 *
 *  THE SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT
 *  WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING 
 *  WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, FITNESS 
 *  FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO
 *  EVENT SHALL LICENSOR BE LIABLE FOR ANY INCIDENTAL, SPECIAL,
 *  INDIRECT OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, 
 *  COST OF PROCUREMENT OF SUBSTITUTE GOODS, TECHNOLOGY OR 
 *  SERVICES, ANY CLAIMS BY THIRD PARTIES (INCLUDING BUT NOT 
 *  LIMITED TO ANY DEFENSE THEREOF), ANY CLAIMS FOR INDEMNITY OR
 *  CONTRIBUTION, OR OTHER SIMILAR COSTS, WHETHER ASSERTED ON THE
 *  BASIS OF CONTRACT, TORT (INCLUDING NEGLIGENCE), BREACH OF
 *  WARRANTY, OR OTHERWISE.
 *
 *********************************************************************/

#ifndef YTHREAD_H
#define YTHREAD_H
#include "ydef.h"

/*********************************************************************
 * EVENT FUNCTIONS
 *********************************************************************/

#ifdef WINDOWS_API
#if defined(__BORLANDC__)
#pragma warn -8019
#include <windows.h>
#pragma warn +8019
#else
#include <windows.h>
#endif
typedef HANDLE yEvent;
#elif defined(FREERTOS_API)
#include "event_groups.h"
typedef struct {
    EventGroupHandle_t handle;
    StaticEventGroup_t buffer;
} yEvent;
#else
#include <pthread.h>
typedef struct {
    pthread_cond_t   cond;
    pthread_mutex_t  mtx;
    int              verif;
    int              autoreset;
} yEvent;
#endif

void yCreateEvent(yEvent* ev);
void yCreateManualEvent(yEvent* event, int initialState);
void ySetEvent(yEvent* ev);
void yResetEvent(yEvent* ev);
int yWaitForEvent(yEvent* ev, int time);
void yCloseEvent(yEvent* ev);


/*********************************************************************
 * THREAD FUNCTIONS
 *********************************************************************/
#ifdef WIN32
typedef HANDLE              osThread;
#define yCurrentThread()    GetCurrentThread()
#else
typedef pthread_t           osThread;
#define yCurrentThread()    pthread_self()
#endif

typedef enum {
    YTHREAD_NOT_STARTED=0,
    YTHREAD_RUNNING,
    YTHREAD_MUST_STOP,
    YTHREAD_STOPED
} YTHREAD_STATE;


typedef struct {
    void* ctx;
    yEvent ev;
    YTHREAD_STATE st;
    osThread th;
} yThread;

#ifdef  __cplusplus
extern "C" {
#endif

int yCreateDetachedThreadNamed(const char* name, void* (*fun)(void*), void* arg);
int yThreadCreateNamed(yThread* yth, const char* name, void* (*fun)(void*), void* arg);
#define yCreateDetachedThread(fun, arg) yCreateDetachedThreadNamed(NULL, fun, arg)
#define yThreadCreate(yth, fun, arg) yThreadCreateNamed(yth, NULL, fun, arg)
int yThreadIsRunning(yThread* yth);
void yThreadSignalStart(yThread* yth);
void yThreadSignalEnd(yThread* yth);
void yThreadRequestEnd(yThread* yth);
int yThreadMustEnd(yThread* yth);
void yThreadKill(yThread* yth);
int yThreadIndex(void);

#ifdef  __cplusplus
}
#endif

#endif
