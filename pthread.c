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

#include "pthread.h"
#include <process.h>

/* number of times to spin a thread about to block on a locked mutex before retrying and sleeping if still locked */
#define SPIN_COUNT 0

/* GROUP_AFFINITY struct */
typedef struct
{
    ULONG_PTR mask; // KAFFINITY = ULONG_PTR
    USHORT group;
    USHORT reserved[3];
} group_affinity_t;

typedef struct
{
    /* global mutex for replacing MUTEX_INITIALIZER instances */
    pthread_mutex_t static_mutex;

    /* function pointers to conditional variable API on windows 6.0+ kernels */
    void (WINAPI *cond_broadcast)( pthread_cond_t *cond );
    void (WINAPI *cond_init)( pthread_cond_t *cond );
    void (WINAPI *cond_signal)( pthread_cond_t *cond );
    BOOL (WINAPI *cond_wait)( pthread_cond_t *cond, pthread_mutex_t *mutex, DWORD milliseconds );
} win32thread_control_t;

static win32thread_control_t thread_control;

/* _beginthreadex requires that the start routine is __stdcall */
static unsigned __stdcall win32thread_worker( void *arg )
{
    pthread_t *h = arg;
    h->ret = h->func( h->arg );
    return 0;
}

int pthread_create( pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)( void* ), void *arg )
{
    thread->func   = start_routine;
    thread->arg    = arg;
    thread->handle = (void*)_beginthreadex( NULL, 0, win32thread_worker, thread, 0, NULL );
    return !thread->handle;
}

int pthread_join( pthread_t thread, void **value_ptr )
{
    DWORD ret = WaitForSingleObject( thread.handle, INFINITE );
    if( ret != WAIT_OBJECT_0 )
        return -1;
    if( value_ptr )
        *value_ptr = thread.ret;
    CloseHandle( thread.handle );
    return 0;
}

int pthread_mutex_init( pthread_mutex_t *mutex, const pthread_mutexattr_t *attr )
{
    return !InitializeCriticalSectionAndSpinCount( mutex, SPIN_COUNT );
}

int pthread_mutex_destroy( pthread_mutex_t *mutex )
{
    DeleteCriticalSection( mutex );
    return 0;
}

int pthread_mutex_lock( pthread_mutex_t *mutex )
{
    static pthread_mutex_t init = PTHREAD_MUTEX_INITIALIZER;
    if( !memcmp( mutex, &init, sizeof(pthread_mutex_t) ) )
        *mutex = thread_control.static_mutex;
    EnterCriticalSection( mutex );
    return 0;
}

int pthread_mutex_unlock( pthread_mutex_t *mutex )
{
    LeaveCriticalSection( mutex );
    return 0;
}

/* for pre-Windows 6.0 platforms we need to define and use our own condition variable and api */
typedef struct
{
    pthread_mutex_t mtx_broadcast;
    pthread_mutex_t mtx_waiter_count;
    int waiter_count;
    HANDLE semaphore;
    HANDLE waiters_done;
    int is_broadcast;
} win32_cond_t;

int pthread_cond_init( pthread_cond_t *cond, const pthread_condattr_t *attr )
{
    if( thread_control.cond_init )
    {
        thread_control.cond_init( cond );
        return 0;
    }

    /* non native condition variables */
    win32_cond_t *win32_cond = calloc( 1, sizeof(win32_cond_t) );
    if( !win32_cond )
        return -1;
    cond->ptr = win32_cond;
    win32_cond->semaphore = CreateSemaphore( NULL, 0, 0x7fffffff, NULL );
    if( !win32_cond->semaphore )
        return -1;

    if( pthread_mutex_init( &win32_cond->mtx_waiter_count, NULL ) )
        return -1;
    if( pthread_mutex_init( &win32_cond->mtx_broadcast, NULL ) )
        return -1;

    win32_cond->waiters_done = CreateEvent( NULL, FALSE, FALSE, NULL );
    if( !win32_cond->waiters_done )
        return -1;

    return 0;
}

int pthread_cond_destroy( pthread_cond_t *cond )
{
    /* native condition variables do not destroy */
    if( thread_control.cond_init )
        return 0;

    /* non native condition variables */
    win32_cond_t *win32_cond = cond->ptr;
    CloseHandle( win32_cond->semaphore );
    CloseHandle( win32_cond->waiters_done );
    pthread_mutex_destroy( &win32_cond->mtx_broadcast );
    pthread_mutex_destroy( &win32_cond->mtx_waiter_count );
    free( win32_cond );

    return 0;
}

int pthread_cond_broadcast( pthread_cond_t *cond )
{
    if( thread_control.cond_broadcast )
    {
        thread_control.cond_broadcast( cond );
        return 0;
    }

    /* non native condition variables */
    win32_cond_t *win32_cond = cond->ptr;
    pthread_mutex_lock( &win32_cond->mtx_broadcast );
    pthread_mutex_lock( &win32_cond->mtx_waiter_count );
    int have_waiter = 0;

    if( win32_cond->waiter_count )
    {
        win32_cond->is_broadcast = 1;
        have_waiter = 1;
    }

    if( have_waiter )
    {
        ReleaseSemaphore( win32_cond->semaphore, win32_cond->waiter_count, NULL );
        pthread_mutex_unlock( &win32_cond->mtx_waiter_count );
        WaitForSingleObject( win32_cond->waiters_done, INFINITE );
        win32_cond->is_broadcast = 0;
    }
    else
        pthread_mutex_unlock( &win32_cond->mtx_waiter_count );
    return pthread_mutex_unlock( &win32_cond->mtx_broadcast );
}

int pthread_cond_signal( pthread_cond_t *cond )
{
    if( thread_control.cond_signal )
    {
        thread_control.cond_signal( cond );
        return 0;
    }

    /* non-native condition variables */
    win32_cond_t *win32_cond = cond->ptr;
    pthread_mutex_lock( &win32_cond->mtx_waiter_count );
    int have_waiter = win32_cond->waiter_count;
    pthread_mutex_unlock( &win32_cond->mtx_waiter_count );

    if( have_waiter )
        ReleaseSemaphore( win32_cond->semaphore, 1, NULL );
    return 0;
}

int pthread_cond_wait( pthread_cond_t *cond, pthread_mutex_t *mutex )
{
    if( thread_control.cond_wait )
        return !thread_control.cond_wait( cond, mutex, INFINITE );

    /* non native condition variables */
    win32_cond_t *win32_cond = cond->ptr;

    pthread_mutex_lock( &win32_cond->mtx_broadcast );
    pthread_mutex_unlock( &win32_cond->mtx_broadcast );

    pthread_mutex_lock( &win32_cond->mtx_waiter_count );
    win32_cond->waiter_count++;
    pthread_mutex_unlock( &win32_cond->mtx_waiter_count );

    // unlock the external mutex
    pthread_mutex_unlock( mutex );
    WaitForSingleObject( win32_cond->semaphore, INFINITE );

    pthread_mutex_lock( &win32_cond->mtx_waiter_count );
    win32_cond->waiter_count--;
    int last_waiter = !win32_cond->waiter_count && win32_cond->is_broadcast;
    pthread_mutex_unlock( &win32_cond->mtx_waiter_count );

    if( last_waiter )
        SetEvent( win32_cond->waiters_done );

    // lock the external mutex
    return pthread_mutex_lock( mutex );
}

int win32_threading_init( void )
{
    /* find function pointers to API functions, if they exist */
    HANDLE kernel_dll = GetModuleHandle( TEXT( "kernel32.dll" ) );
    thread_control.cond_init = (void*)GetProcAddress( kernel_dll, "InitializeConditionVariable" );
    if( thread_control.cond_init )
    {
        /* we're on a windows 6.0+ kernel, acquire the rest of the functions */
        thread_control.cond_broadcast = (void*)GetProcAddress( kernel_dll, "WakeAllConditionVariable" );
        thread_control.cond_signal = (void*)GetProcAddress( kernel_dll, "WakeConditionVariable" );
        thread_control.cond_wait = (void*)GetProcAddress( kernel_dll, "SleepConditionVariableCS" );
    }
    return pthread_mutex_init( &thread_control.static_mutex, NULL );
}

void win32_threading_destroy( void )
{
    pthread_mutex_destroy( &thread_control.static_mutex );
    memset( &thread_control, 0, sizeof(win32thread_control_t) );
}

int pthread_num_processors_np()
{
    DWORD_PTR system_cpus, process_cpus = 0;
    int cpus = 0;

    /* GetProcessAffinityMask returns affinities of 0 when the process has threads in multiple processor groups.
     * On platforms that support processor grouping, use GetThreadGroupAffinity to get the current thread's affinity instead. */
#ifdef _WIN64
    /* find function pointers to API functions specific to x86_64 platforms, if they exist */
    HANDLE kernel_dll = GetModuleHandle( TEXT( "kernel32.dll" ) );
    BOOL (*get_thread_affinity)( HANDLE thread, group_affinity_t *group_affinity ) = (void*)GetProcAddress( kernel_dll, "GetThreadGroupAffinity" );
    if( get_thread_affinity )
    {
        /* running on a platform that supports >64 logical cpus */
        group_affinity_t thread_affinity;
        if( get_thread_affinity( GetCurrentThread(), &thread_affinity ) )
            process_cpus = thread_affinity.mask;
    }
#endif
    if( !process_cpus )
        GetProcessAffinityMask( GetCurrentProcess(), &process_cpus, &system_cpus );
    for( DWORD_PTR bit = 1; bit; bit <<= 1 )
        cpus += !!(process_cpus & bit);

    return cpus ? cpus : 1;
}

static void on_process_init(void)
{
    win32_threading_init();
}

static void on_process_exit(void)
{
    win32_threading_destroy();
}

#if defined(__MINGW64__) || defined(__MINGW32__)
# define attribute_section(a) __attribute__((section(a)))
#elif defined(_MSC_VER)
# define attribute_section(a) __pragma(section(a,long,read)); __declspec(allocate(a))
#endif

attribute_section(".ctors") void *gcc_ctor = on_process_init;
attribute_section(".dtors") void *gcc_dtor = on_process_exit;

attribute_section(".CRT$XCU") void *msc_ctor = on_process_init;
attribute_section(".CRT$XPU") void *msc_dtor = on_process_exit;
