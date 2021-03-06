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

/** \file os/vxworks6.6/code/os_time.c
 *  \brief VxWorks RTP time management
 *
 * Implements time management for VxWorks RTP
 * by including the common services and
 * the SVR4 os_timeGet implementation and
 * the POSIX os_nanoSleep implementation
 */

#include "../common/code/os_time.c"
#include "../posix/code/os_time.c"
#include "../common/code/os_time_ctime.c"
