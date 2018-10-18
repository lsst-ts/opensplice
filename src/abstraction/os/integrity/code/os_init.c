/*
 *                         Vortex OpenSplice
 *
 *   This software and documentation are Copyright 2006 to TO_YEAR ADLINK
 *   Technology Limited, its affiliated companies and licensors. All rights
 *   reserved.
 *
 *   Licensed under the ADLINK Software License Agreement Rev 2.7 2nd October
 *   2014 (the "License"); you may not use this file except in compliance with
 *   the License.
 *   You may obtain a copy of the License at:
 *                      $OSPL_HOME/LICENSE
 *
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

/****************************************************************
 * Initialization / Deinitialization                            *
 ****************************************************************/

/** \file os/int509/code/os_init.c
 *  \brief Initialization / Deinitialization
 */

#include "code/os__sharedmem.h"
#include "code/os__thread.h"
#include "os_defs.h"
#include "os__mutex.h"
#include "os_mutex.h"
#include "os__cond.h"
#include "os_abstract.h"
#include "os_report.h"
#include "os_atomics.h"
//#include "os_stdlib.h"

/** \brief Counter that keeps track of number of times os-layer is initialized */
static pa_uint32_t _ospl_osInitCount = PA_UINT32_INIT(0);

/** \brief OS layer initialization
 *
 * \b os_osInit calls:
 * - \b os_sharedMemoryInit
 * - \b os_threadInit
 */
void
os_osInit (
    void)
{
    os_result result;
    os_uint32 initCount;

    initCount = pa_inc32_nv(&_ospl_osInitCount);

    if (initCount == 1) {
        os_reportInit(OS_FALSE);
        os_threadModuleInit();
        os_mutexModuleInit();
        os_condModuleInit();

        result = os_mutexInit(&strerrorMutex, NULL);
        if(result != os_resultSuccess) {
            OS_REPORT(OS_ERROR, "os_osInit", 1,
                    "os_mutexInit of strerror mutex failed");
        }
    }

    return;
}

/** \brief OS layer deinitialization
 *
 * \b os_osExit calls:
 * - \b os_sharedMemoryExit
 * - \b os_threadExit
 */
void
os_osExit (
    void)
{
    os_uint32 initCount;

    initCount = pa_dec32_nv(&_ospl_osInitCount);

    if (initCount == 0) {
        os_threadModuleExit();
        os_mutexModuleExit();
        os_condModuleExit();
        os_reportExit();

        os_mutexDestroy(&strerrorMutex);

    } else if ((initCount + 1) < initCount){
        /* The 0 boundary is passed, so os_osExit is called more often than
         * os_osInit. Therefore undo decrement as nothing happened and warn. */
        initCount = pa_inc32_nv(&_ospl_osInitCount);
        OS_REPORT(OS_WARNING, "os_osExit", 1, "OS-layer not initialized");
        /* Fail in case of DEV, as it is incorrect API usage */
        assert(0);
    }
    return;
}

#include "../common/code/os_service.c"
