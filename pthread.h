/*****************************************************************************
 * Copyright (C) 2011 x264 project (bulk of the code)
 *                    Pthreads-win32 contributors (autostatic stuff)
 *                    Alexander Smith (modifications to allow building as lib)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *****************************************************************************/

#ifndef PTHREAD_H
#define PTHREAD_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct
{
    void *handle;
    void *(*func)( void* arg );
    void *arg;
    void *ret;
} pthread_t;
#define pthread_attr_t int

/* the conditional variable api for windows 6.0+ uses critical sections and not mutexes */
typedef CRITICAL_SECTION pthread_mutex_t;
#define PTHREAD_MUTEX_INITIALIZER {0}
#define pthread_mutexattr_t int

/* This is the CONDITIONAL_VARIABLE typedef for using Window's native conditional variables on kernels 6.0+.
 * MinGW does not currently have this typedef. */
typedef struct
{
    void *ptr;
} pthread_cond_t;
#define pthread_condattr_t int

int pthread_create( pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)( void* ), void *arg );
int pthread_join( pthread_t thread, void **value_ptr );

int pthread_mutex_init( pthread_mutex_t *mutex, const pthread_mutexattr_t *attr );
int pthread_mutex_destroy( pthread_mutex_t *mutex );
int pthread_mutex_lock( pthread_mutex_t *mutex );
int pthread_mutex_unlock( pthread_mutex_t *mutex );

int pthread_cond_init( pthread_cond_t *cond, const pthread_condattr_t *attr );
int pthread_cond_destroy( pthread_cond_t *cond );
int pthread_cond_broadcast( pthread_cond_t *cond );
int pthread_cond_wait( pthread_cond_t *cond, pthread_mutex_t *mutex );
int pthread_cond_signal( pthread_cond_t *cond );

#define pthread_attr_init(a) 0
#define pthread_attr_destroy(a) 0

//int  win32_threading_init( void );
//void win32_threading_destroy( void );

int pthread_num_processors_np( void );

#endif
