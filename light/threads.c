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

#ifdef USE_PTHREADS
#define MAX_THREADS 32
pthread_mutex_t *my_mutex;
#else
#define MAX_THREADS 1
#endif

int numthreads = 1;

void
InitThreads(void)
{
#ifdef USE_PTHREADS
    int status;
    pthread_mutexattr_t mattrib;

    my_mutex = malloc(sizeof(*my_mutex));
    status = pthread_mutexattr_init(&mattrib);
    if (status)
	Error("pthread_mutexattr_init failed");

    status = pthread_mutex_init(my_mutex, &mattrib);
    if (status)
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
#ifdef USE_PTHREADS
    int status;
    pthread_t threads[MAX_THREADS];
    pthread_attr_t attrib;
    int i;

    if (numthreads == 1) {
	func(NULL);
	return;
    }

    status = pthread_attr_init(&attrib);
    if (status)
	Error("pthread_attr_init failed");
#if 0
    if (pthread_attr_setstacksize(&attrib, 0x100000) == -1)
	Error("pthread_attr_setstacksize failed");
#endif

    for (i = 0; i < numthreads; i++) {
	status = pthread_create(&threads[i], &attrib, func, NULL);
	if (status)
	    Error("pthread_create failed");
    }

    for (i = 0; i < numthreads; i++) {
	status = pthread_join(threads[i], NULL);
	if (status)
	    Error("pthread_join failed");
    }

    status = pthread_mutex_destroy(my_mutex);
    if (status)
	Error("pthread_mutex_destroy failed");

#else
    func(NULL);
#endif
}
