/*  Copyright (C) 1996-1997  Id Software, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#include <common/cmdlib.h>
#include <light/threads.h>

#ifdef __alpha
static pthread_mutex_t *my_mutex;
int numthreads = 4;
#else
int numthreads = 1;
#endif

void
InitThreads(void)
{
#ifdef __alpha
    pthread_mutexattr_t mattrib;

    my_mutex = malloc(sizeof(*my_mutex));
    if (pthread_mutexattr_create(&mattrib) == -1)
	Error("pthread_mutex_attr_create failed");
    if (pthread_mutexattr_setkind_np(&mattrib, MUTEX_FAST_NP) == -1)
	Error("pthread_mutexattr_setkind_np failed");
    if (pthread_mutex_init(my_mutex, mattrib) == -1)
	Error("pthread_mutex_init failed");
#endif
}

/*
 * ===============
 * RunThreadsOn
 * ===============
 */
void
RunThreadsOn(threadfunc_t func)
{
#ifdef __alpha
    pthread_t work_threads[256];
    pthread_addr_t status;
    pthread_attr_t attrib;
    int i;

    if (numthreads == 1) {
	func(NULL);
	return;
    }

    if (pthread_attr_create(&attrib) == -1)
	Error("pthread_attr_create failed");
    if (pthread_attr_setstacksize(&attrib, 0x100000) == -1)
	Error("pthread_attr_setstacksize failed");

    for (i = 0; i < numthreads; i++) {
	if (pthread_create(&work_threads[i], attrib,
			   (pthread_startroutine_t) func,
			   (pthread_addr_t) i) == -1)
	    Error("pthread_create failed");
    }

    for (i = 0; i < numthreads; i++) {
	if (pthread_join(work_threads[i], &status) == -1)
	    Error("pthread_join failed");
    }
#else
    func(NULL);
#endif
}
