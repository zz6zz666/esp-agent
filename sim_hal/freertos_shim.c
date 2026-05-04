/*
 * freertos_shim.c — FreeRTOS → POSIX pthread mapping for desktop simulator
 *
 * Maps FreeRTOS tasks/queues/semaphores/timers to pthread primitives.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "freertos/FreeRTOS.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <unistd.h>  /* usleep, getpid (MinGW + POSIX) */

#if defined(PLATFORM_WINDOWS)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif

/* ================================================================
 *  Tick counter
 * ================================================================ */

static TickType_t _tick_count(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (TickType_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

TickType_t xTaskGetTickCount(void)
{
    return _tick_count();
}

/* ================================================================
 *  Task wrapper
 * ================================================================ */

typedef struct {
    TaskFunction_t func;
    void *arg;
    char name[32];
    pthread_t thread;
    bool running;
    bool detached;
} task_wrapper_t;

static task_wrapper_t *task_wrap_create(TaskFunction_t func, const char *name, void *arg)
{
    task_wrapper_t *tw = calloc(1, sizeof(*tw));
    if (!tw) return NULL;
    tw->func = func;
    tw->arg = arg;
    if (name) {
        strncpy(tw->name, name, sizeof(tw->name) - 1);
        tw->name[sizeof(tw->name) - 1] = '\0';
    }
    tw->running = false;
    tw->detached = false;
    return tw;
}

static void *task_thread_entry(void *arg)
{
    task_wrapper_t *tw = (task_wrapper_t *)arg;
    tw->running = true;

    /* Set thread name (best-effort, platform-specific) */
#if defined(__linux__)
    pthread_setname_np(pthread_self(), tw->name);
#elif defined(_WIN32)
    /* MinGW winpthreads may support pthread_setname_np; skip if not */
    (void)tw->name;
#endif

    tw->func(tw->arg);

    tw->running = false;
    return NULL;
}

/* ================================================================
 *  Task API
 * ================================================================ */

BaseType_t xTaskCreate(TaskFunction_t task_func,
                       const char *name,
                       uint32_t stack_size,
                       void *arg,
                       UBaseType_t priority,
                       TaskHandle_t *task_handle)
{
    return xTaskCreatePinnedToCore(task_func, name, stack_size, arg, priority, task_handle, tskNO_AFFINITY);
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task_func,
                                   const char *name,
                                   uint32_t stack_size,
                                   void *arg,
                                   UBaseType_t priority,
                                   TaskHandle_t *task_handle,
                                   BaseType_t core_id)
{
    task_wrapper_t *tw;
    pthread_attr_t attr;

    (void)stack_size;
    (void)core_id;
    (void)priority;

    tw = task_wrap_create(task_func, name, arg);
    if (!tw) return pdFAIL;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    int rc = pthread_create(&tw->thread, &attr, task_thread_entry, tw);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        free(tw);
        return pdFAIL;
    }

    if (task_handle) {
        *task_handle = (TaskHandle_t)tw;
    } else {
        /* Caller doesn't want the handle — detach so resources are freed on exit */
        tw->detached = true;
        pthread_detach(tw->thread);
    }

    return pdPASS;
}

BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t task_func,
                                           const char *name,
                                           uint32_t stack_size,
                                           void *arg,
                                           UBaseType_t priority,
                                           TaskHandle_t *task_handle,
                                           BaseType_t core_id,
                                           UBaseType_t caps)
{
    (void)caps;
    return xTaskCreatePinnedToCore(task_func, name, stack_size, arg, priority, task_handle, core_id);
}

BaseType_t xTaskCreateWithCaps(TaskFunction_t task_func,
                                const char *name,
                                uint32_t stack_size,
                                void *arg,
                                UBaseType_t priority,
                                TaskHandle_t *task_handle,
                                UBaseType_t caps)
{
    (void)caps;
    return xTaskCreatePinnedToCore(task_func, name, stack_size, arg, priority, task_handle, tskNO_AFFINITY);
}

void vTaskDelete(TaskHandle_t task_handle)
{
    task_wrapper_t *tw = (task_wrapper_t *)task_handle;
    if (!tw) {
        /* Deleting self */
        pthread_exit(NULL);
        return;
    }

    if (!tw->detached) {
        pthread_cancel(tw->thread);
        pthread_join(tw->thread, NULL);
    }
    free(tw);
}

void vTaskDelay(TickType_t ticks)
{
    usleep((useconds_t)ticks * 1000);
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    return (TaskHandle_t)pthread_self();
}

/* ================================================================
 *  Queue API  (ring buffer + mutex + condvar)
 * ================================================================ */

typedef struct {
    void *buffer;
    size_t item_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    pthread_mutex_t mutex;
    pthread_cond_t cond_send;
    pthread_cond_t cond_recv;
} queue_t;

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size)
{
    queue_t *q = calloc(1, sizeof(*q));
    if (!q) return NULL;

    q->item_size = item_size;
    q->capacity = queue_length;
    q->buffer = calloc(queue_length, item_size);
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_send, NULL);
    pthread_cond_init(&q->cond_recv, NULL);

    return (QueueHandle_t)q;
}

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait)
{
    queue_t *q = (queue_t *)queue;
    struct timespec ts;
    int rc;

    if (!q) return pdFAIL;

    pthread_mutex_lock(&q->mutex);

    /* Wait while full */
    while (q->count >= q->capacity) {
        if (ticks_to_wait == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFAIL;
        }
        if (ticks_to_wait == portMAX_DELAY) {
            pthread_cond_wait(&q->cond_send, &q->mutex);
        } else {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ticks_to_wait / 1000;
            ts.tv_nsec += (ticks_to_wait % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            rc = pthread_cond_timedwait(&q->cond_send, &q->mutex, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFAIL;
            }
        }
    }

    memcpy((char *)q->buffer + (q->tail * q->item_size), item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;

    pthread_cond_signal(&q->cond_recv);
    pthread_mutex_unlock(&q->mutex);
    return pdPASS;
}

BaseType_t xQueueReceive(QueueHandle_t queue, void *buffer, TickType_t ticks_to_wait)
{
    queue_t *q = (queue_t *)queue;
    struct timespec ts;
    int rc;

    if (!q) return pdFAIL;

    pthread_mutex_lock(&q->mutex);

    /* Wait while empty */
    while (q->count == 0) {
        if (ticks_to_wait == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFAIL;
        }
        if (ticks_to_wait == portMAX_DELAY) {
            pthread_cond_wait(&q->cond_recv, &q->mutex);
        } else {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ticks_to_wait / 1000;
            ts.tv_nsec += (ticks_to_wait % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            rc = pthread_cond_timedwait(&q->cond_recv, &q->mutex, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&q->mutex);
                return pdFAIL;
            }
        }
    }

    memcpy(buffer, (char *)q->buffer + (q->head * q->item_size), q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;

    pthread_cond_signal(&q->cond_send);
    pthread_mutex_unlock(&q->mutex);
    return pdPASS;
}

void vQueueDelete(QueueHandle_t queue)
{
    queue_t *q = (queue_t *)queue;
    if (!q) return;

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond_send);
    pthread_cond_destroy(&q->cond_recv);
    free(q->buffer);
    free(q);
}

/* ================================================================
 *  Semaphore API
 * ================================================================ */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned int count;
    bool is_mutex;       /* true if created via xSemaphoreCreateMutex */
    bool is_recursive;   /* true if created via xSemaphoreCreateRecursiveMutex */
    pthread_t owner;     /* owning thread (recursive only) */
    unsigned int recursion; /* recursion depth (recursive only) */
} sem_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    sem_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;

    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->count = 1;
    s->is_mutex = true;
    return (SemaphoreHandle_t)s;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks_to_wait)
{
    sem_t *s = (sem_t *)sem;
    struct timespec ts;
    int rc;

    if (!s) return pdFAIL;

    pthread_mutex_lock(&s->mutex);

    while (s->count == 0) {
        if (ticks_to_wait == 0) {
            pthread_mutex_unlock(&s->mutex);
            return pdFAIL;
        }
        if (ticks_to_wait == portMAX_DELAY) {
            pthread_cond_wait(&s->cond, &s->mutex);
        } else {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ticks_to_wait / 1000;
            ts.tv_nsec += (ticks_to_wait % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            rc = pthread_cond_timedwait(&s->cond, &s->mutex, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&s->mutex);
                return pdFAIL;
            }
        }
    }

    s->count--;
    pthread_mutex_unlock(&s->mutex);
    return pdPASS;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t sem)
{
    sem_t *s = (sem_t *)sem;
    if (!s) return pdFAIL;

    pthread_mutex_lock(&s->mutex);
    s->count++;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return pdPASS;
}

void vSemaphoreDelete(SemaphoreHandle_t sem)
{
    sem_t *s = (sem_t *)sem;
    if (!s) return;

    pthread_mutex_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    sem_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutex_init(&s->mutex, NULL);
    pthread_cond_init(&s->cond, NULL);
    s->count = 0; /* binary sem starts at 0 (taken) */
    s->is_mutex = false;
    return (SemaphoreHandle_t)s;
}

SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    sem_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&s->mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    pthread_cond_init(&s->cond, NULL);
    s->count = 1;
    s->is_mutex = true;
    s->is_recursive = true;
    s->recursion = 0;
    return (SemaphoreHandle_t)s;
}

BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks_to_wait)
{
    sem_t *s = (sem_t *)sem;
    struct timespec ts;
    int rc;

    if (!s || !s->is_recursive) return pdFAIL;

    pthread_mutex_lock(&s->mutex);

    /* Already held by this thread — bump recursion */
    if (s->count == 0 && pthread_equal(s->owner, pthread_self())) {
        s->recursion++;
        pthread_mutex_unlock(&s->mutex);
        return pdPASS;
    }

    /* Wait for availability (same as normal Take) */
    while (s->count == 0) {
        if (ticks_to_wait == 0) {
            pthread_mutex_unlock(&s->mutex);
            return pdFAIL;
        }
        if (ticks_to_wait == portMAX_DELAY) {
            pthread_cond_wait(&s->cond, &s->mutex);
        } else {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += ticks_to_wait / 1000;
            ts.tv_nsec += (ticks_to_wait % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) {
                ts.tv_sec += 1;
                ts.tv_nsec -= 1000000000L;
            }
            rc = pthread_cond_timedwait(&s->cond, &s->mutex, &ts);
            if (rc == ETIMEDOUT) {
                pthread_mutex_unlock(&s->mutex);
                return pdFAIL;
            }
        }
    }

    /* First acquisition */
    s->count = 0;
    s->owner = pthread_self();
    s->recursion = 1;
    pthread_mutex_unlock(&s->mutex);
    return pdPASS;
}

BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem)
{
    sem_t *s = (sem_t *)sem;
    if (!s || !s->is_recursive) return pdFAIL;

    pthread_mutex_lock(&s->mutex);

    if (s->count != 0 || !pthread_equal(s->owner, pthread_self())) {
        /* Not held, or held by another thread */
        pthread_mutex_unlock(&s->mutex);
        return pdFAIL;
    }

    if (s->recursion > 1) {
        s->recursion--;
        pthread_mutex_unlock(&s->mutex);
        return pdPASS;
    }

    /* Last release */
    s->owner = 0;
    s->recursion = 0;
    s->count = 1;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->mutex);
    return pdPASS;
}

/* ================================================================
 *  Task notifications (minimal — stub with a per-task counter)
 *  Used by some capabilities for light synchronization.
 * ================================================================ */

static pthread_key_t _notif_key;
static pthread_once_t _notif_key_once = PTHREAD_ONCE_INIT;

static void _notif_key_free(void *val)
{
    free(val);
}

static void _notif_key_init(void)
{
    pthread_key_create(&_notif_key, _notif_key_free);
}

static uint32_t *_notif_get(void)
{
    pthread_once(&_notif_key_once, _notif_key_init);
    uint32_t *p = (uint32_t *)pthread_getspecific(_notif_key);
    if (!p) {
        p = calloc(1, sizeof(*p));
        pthread_setspecific(_notif_key, p);
    }
    return p;
}

UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task)
{
    (void)task;
    return 4096; /* plenty for desktop sim */
}

BaseType_t xTaskNotifyGive(TaskHandle_t task)
{
    (void)task;
    uint32_t *p = _notif_get();
    (*p)++;
    return pdPASS;
}

BaseType_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks_to_wait)
{
    uint32_t *p = _notif_get();
    (void)ticks_to_wait;

    if (*p == 0) {
        /* In a real system this would block. For simulation, just yield. */
        usleep(1000);
        return 0;
    }

    uint32_t val = *p;
    if (clear_on_exit) {
        *p = 0;
    }
    return (BaseType_t)val;
}

/* ================================================================
 *  Event Groups
 *   — pthread mutex + cond + bitmask
 * ================================================================ */

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
    EventBits_t     bits;
} event_group_t;

EventGroupHandle_t xEventGroupCreate(void)
{
    event_group_t *eg = calloc(1, sizeof(*eg));
    if (!eg) return NULL;
    pthread_mutex_init(&eg->mutex, NULL);
    pthread_cond_init(&eg->cond, NULL);
    return (EventGroupHandle_t)eg;
}

void vEventGroupDelete(EventGroupHandle_t eg)
{
    event_group_t *e = (event_group_t *)eg;
    if (!e) return;
    pthread_mutex_destroy(&e->mutex);
    pthread_cond_destroy(&e->cond);
    free(e);
}

EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t bits)
{
    event_group_t *e = (event_group_t *)eg;
    if (!e) return 0;
    pthread_mutex_lock(&e->mutex);
    e->bits |= bits;
    pthread_cond_broadcast(&e->cond);
    EventBits_t result = e->bits;
    pthread_mutex_unlock(&e->mutex);
    return result;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t eg, EventBits_t bits)
{
    event_group_t *e = (event_group_t *)eg;
    if (!e) return 0;
    pthread_mutex_lock(&e->mutex);
    e->bits &= ~bits;
    EventBits_t result = e->bits;
    pthread_mutex_unlock(&e->mutex);
    return result;
}

EventBits_t xEventGroupGetBits(EventGroupHandle_t eg)
{
    event_group_t *e = (event_group_t *)eg;
    if (!e) return 0;
    pthread_mutex_lock(&e->mutex);
    EventBits_t result = e->bits;
    pthread_mutex_unlock(&e->mutex);
    return result;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg,
                                EventBits_t bits_to_wait_for,
                                BaseType_t clear_on_exit,
                                BaseType_t wait_for_all_bits,
                                TickType_t ticks_to_wait)
{
    event_group_t *e = (event_group_t *)eg;
    if (!e) return 0;

    pthread_mutex_lock(&e->mutex);
    while (1) {
        EventBits_t match = e->bits & bits_to_wait_for;
        bool ready = wait_for_all_bits
            ? (match == bits_to_wait_for)
            : (match != 0);
        if (ready) {
            if (clear_on_exit) e->bits &= ~bits_to_wait_for;
            EventBits_t result = e->bits;
            pthread_mutex_unlock(&e->mutex);
            return result;
        }
        if (ticks_to_wait == portMAX_DELAY) {
            pthread_cond_wait(&e->cond, &e->mutex);
        } else {
            struct timespec ts;
            struct timeval tv;
            gettimeofday(&tv, NULL);
            uint64_t ns = (uint64_t)(tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL) +
                          (uint64_t)ticks_to_wait * 1000000ULL;
            ts.tv_sec = ns / 1000000000ULL;
            ts.tv_nsec = ns % 1000000000ULL;
            int rc = pthread_cond_timedwait(&e->cond, &e->mutex, &ts);
            if (rc == ETIMEDOUT) {
                EventBits_t result = e->bits;
                pthread_mutex_unlock(&e->mutex);
                return result;
            }
        }
    }
}

BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t eg, EventBits_t bits,
                                      BaseType_t *pxHigherPriorityTaskWoken)
{
    (void)pxHigherPriorityTaskWoken;
    xEventGroupSetBits(eg, bits);
    return pdPASS;
}

/* ================================================================
 *  Runtime stats — read real thread info
 *    Linux: /proc/stat + /proc/self/task/<tid>/stat
 *    Windows: GetSystemTimes() + CreateToolhelp32Snapshot + GetThreadTimes()
 * ================================================================ */

#if !defined(PLATFORM_WINDOWS)
# include <dirent.h>
#endif

#define TASK_MAX_NAME_LEN 32

/*
 * Thread name table.
 * Each entry corresponds to a pthread / Windows thread.
 * Populated on first uxTaskGetSystemState() call, refreshed on each call.
 */
typedef struct {
    unsigned long tid;
    char          name[TASK_MAX_NAME_LEN];
    uint64_t      utime_ticks;  /* CPU time (units: POSIX jiffies, Windows 100ns→usec) */
    uint64_t      stime_ticks;  /* kernel CPU time */
} thread_entry_t;

static thread_entry_t *s_threads = NULL;
static UBaseType_t     s_thread_count = 0;

/* ================================================================
 *  _host_idle_pct() — system-wide CPU idle percentage [0.0, 100.0]
 * ================================================================ */
#if defined(PLATFORM_WINDOWS)

static double _host_idle_pct(void)
{
    static ULARGE_INTEGER prev_idle  = {0};
    static ULARGE_INTEGER prev_total = {0};
    FILETIME ft_idle, ft_kernel, ft_user;

    if (!GetSystemTimes(&ft_idle, &ft_kernel, &ft_user))
        return 95.0;

    ULARGE_INTEGER cur_idle, cur_kernel, cur_user;
    cur_idle.LowPart  = ft_idle.dwLowDateTime;
    cur_idle.HighPart = ft_idle.dwHighDateTime;
    cur_kernel.LowPart  = ft_kernel.dwLowDateTime;
    cur_kernel.HighPart = ft_kernel.dwHighDateTime;
    cur_user.LowPart  = ft_user.dwLowDateTime;
    cur_user.HighPart = ft_user.dwHighDateTime;

    ULARGE_INTEGER cur_total;
    cur_total.QuadPart = cur_idle.QuadPart + cur_kernel.QuadPart + cur_user.QuadPart;

    /* First call — need a baseline */
    if (prev_total.QuadPart == 0) {
        prev_idle  = cur_idle;
        prev_total = cur_total;
        return 95.0;
    }

    uint64_t delta_idle  = cur_idle.QuadPart  - prev_idle.QuadPart;
    uint64_t delta_total = cur_total.QuadPart  - prev_total.QuadPart;

    prev_idle  = cur_idle;
    prev_total = cur_total;

    if (delta_total == 0) return 95.0;
    return (double)delta_idle * 100.0 / (double)delta_total;
}

#else /* PLATFORM_LINUX / POSIX */

static double _host_idle_pct(void)
{
    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 95.0;
    char line[512];
    double idle_pct = 95.0;
    if (fgets(line, sizeof(line), f)) {
        unsigned long long user=0, nice=0, system=0, idle=0, iowait=0;
        unsigned long long irq=0, softirq=0, steal=0;
        int n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
        if (n >= 4) {
            unsigned long long total_idle = idle + iowait;
            unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
            if (total > 0)
                idle_pct = (double)total_idle * 100.0 / (double)total;
        }
    }
    fclose(f);
    return idle_pct;
}

#endif /* platform _host_idle_pct */

/* ================================================================
 *  _refresh_threads() — enumerate threads + CPU times
 * ================================================================ */
#if defined(PLATFORM_WINDOWS)

# include <tlhelp32.h>

static void _refresh_threads(void)
{
    free(s_threads);
    s_threads = NULL;
    s_thread_count = 0;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return;

    DWORD my_pid = GetCurrentProcessId();
    THREADENTRY32 te;
    te.dwSize = sizeof(te);

    size_t cap = 0;
    if (Thread32First(hSnap, &te)) {
        do {
            if (te.th32OwnerProcessID != my_pid) continue;
            if (te.th32ThreadID == 0) continue;

            if (cap == s_thread_count) {
                cap = cap ? cap * 2 : 64;
                s_threads = realloc(s_threads, cap * sizeof(*s_threads));
                if (!s_threads) { CloseHandle(hSnap); return; }
            }

            thread_entry_t *td = &s_threads[s_thread_count];
            memset(td, 0, sizeof(*td));
            td->tid = te.th32ThreadID;

            /* Default thread name */
            snprintf(td->name, sizeof(td->name), "thread-%lu", td->tid);

            /* Query per-thread CPU times (kernel + user, in 100ns units).
             * Convert to microseconds (/10) so they stay in a reasonable range
             * while preserving proportional correctness for the stats formula. */
            HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (hThread) {
                FILETIME ft_create, ft_exit, ft_kernel, ft_user;
                if (GetThreadTimes(hThread, &ft_create, &ft_exit, &ft_kernel, &ft_user)) {
                    ULARGE_INTEGER uk, uu;
                    uk.LowPart = ft_kernel.dwLowDateTime;
                    uk.HighPart = ft_kernel.dwHighDateTime;
                    uu.LowPart = ft_user.dwLowDateTime;
                    uu.HighPart = ft_user.dwHighDateTime;
                    td->stime_ticks = uk.QuadPart / 10; /* 100ns → us */
                    td->utime_ticks = uu.QuadPart / 10;
                }
                CloseHandle(hThread);
            }

            s_thread_count++;
        } while (Thread32Next(hSnap, &te));
    }

    CloseHandle(hSnap);
}

#else /* PLATFORM_LINUX / POSIX */

static void _refresh_threads(void)
{
    free(s_threads);
    s_threads = NULL;
    s_thread_count = 0;

    long ticks_per_sec = sysconf(_SC_CLK_TCK);
    if (ticks_per_sec <= 0) ticks_per_sec = 100;

    DIR *d = opendir("/proc/self/task");
    if (!d) return;

    size_t cap = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (cap == s_thread_count) {
            cap = cap ? cap * 2 : 64;
            s_threads = realloc(s_threads, cap * sizeof(*s_threads));
            if (!s_threads) { closedir(d); return; }
        }
        thread_entry_t *te = &s_threads[s_thread_count];
        te->tid = (unsigned long)atoi(ent->d_name);
        te->name[0] = '\0';
        te->utime_ticks = 0;
        te->stime_ticks = 0;

        /* Read /proc/self/task/<tid>/stat for name and CPU times */
        char path[256];
        snprintf(path, sizeof(path), "/proc/self/task/%lu/stat", te->tid);
        FILE *sf = fopen(path, "r");
        if (sf) {
            char comm[64] = {0};
            char state = '?';
            unsigned long long utime=0, stime=0;
            int matched = fscanf(sf, "%*d %63s %c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %llu %llu",
                                 comm, &state, &utime, &stime);
            fclose(sf);

            /* Strip parentheses from comm */
            size_t clen = strlen(comm);
            if (clen >= 2 && comm[0] == '(' && comm[clen-1] == ')') {
                memmove(comm, comm + 1, clen - 2);
                comm[clen - 2] = '\0';
            }
            strncpy(te->name, comm, TASK_MAX_NAME_LEN - 1);

            if (matched == 4 || matched == 5) {
                te->utime_ticks = utime;
                te->stime_ticks = stime;
            }
        }
        s_thread_count++;
    }
    closedir(d);
}

#endif /* platform _refresh_threads */

UBaseType_t uxTaskGetNumberOfTasks(void)
{
    _refresh_threads();
    /* +1 for the synthetic IDLE task we add in uxTaskGetSystemState */
    return s_thread_count + 1;
}

UBaseType_t uxTaskGetSystemState(TaskStatus_t *pxTaskStatusArray,
                                  UBaseType_t uxArraySize,
                                  uint32_t *pulTotalRunTime)
{
    _refresh_threads();

    double host_idle_pct = _host_idle_pct();
    if (host_idle_pct < 1.0) host_idle_pct = 1.0;
    if (host_idle_pct > 99.0) host_idle_pct = 99.0;
    double host_cpu_pct = 100.0 - host_idle_pct;

    /* Compute total runtime from real threads */
    uint64_t real_total_ticks = 0;
    UBaseType_t i;
    for (i = 0; i < s_thread_count; i++) {
        real_total_ticks += s_threads[i].utime_ticks + s_threads[i].stime_ticks;
    }
    if (real_total_ticks == 0) real_total_ticks = 1000;

    /*
     * We want: idle_runtime / (real_total + idle_runtime) = host_idle_pct / 100
     * → idle_runtime = host_idle_pct * real_total / host_cpu_pct
     */
    uint64_t idle_ticks = (uint64_t)(host_idle_pct * (double)real_total_ticks / host_cpu_pct);
    if (idle_ticks > UINT32_MAX) idle_ticks = UINT32_MAX;

    uint64_t total_ticks = real_total_ticks + idle_ticks;
    if (total_ticks > UINT32_MAX) {
        /* Scale down proportionally */
        double scale = (double)UINT32_MAX / (double)total_ticks;
        real_total_ticks = (uint64_t)((double)real_total_ticks * scale);
        idle_ticks = (uint64_t)((double)idle_ticks * scale);
        total_ticks = real_total_ticks + idle_ticks;
    }

    if (pulTotalRunTime) *pulTotalRunTime = (uint32_t)total_ticks;

    /* Fill real tasks */
    UBaseType_t count = 0;
    UBaseType_t limit = uxArraySize > 0 ? uxArraySize - 1 : 0; /* reserve 1 slot for IDLE */
    for (i = 0; i < s_thread_count && count < limit; i++) {
        TaskStatus_t *ts = &pxTaskStatusArray[count];
        memset(ts, 0, sizeof(*ts));
        ts->xHandle = (TaskHandle_t)(uintptr_t)(size_t)s_threads[i].tid;
        ts->pcTaskName = s_threads[i].name[0] ? s_threads[i].name : "pthread";
        ts->xTaskNumber = count;
        ts->eCurrentState = eReady;
        ts->uxCurrentPriority = 1;
        ts->uxBasePriority = 1;
        /* Scale each real task's runtime to fit in the scaled total */
        uint64_t task_ticks = s_threads[i].utime_ticks + s_threads[i].stime_ticks;
        ts->ulRunTimeCounter = (uint32_t)(task_ticks > 0 ? task_ticks : 1);
        count++;
    }

    /* Add synthetic IDLE task(s) so cap_system's formula gives real host CPU% */
    if (count < uxArraySize) {
        TaskStatus_t *ts = &pxTaskStatusArray[count];
        memset(ts, 0, sizeof(*ts));
        ts->xHandle = (TaskHandle_t)(uintptr_t)0;
        ts->pcTaskName = "IDLE0";
        ts->xTaskNumber = count;
        ts->eCurrentState = eRunning;
        ts->uxCurrentPriority = 0;
        ts->uxBasePriority = 0;
        ts->ulRunTimeCounter = (uint32_t)idle_ticks;
        count++;
    }

    return count;
}
