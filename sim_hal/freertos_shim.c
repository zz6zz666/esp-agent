/*
 * freertos_shim.c — FreeRTOS → POSIX pthread mapping for desktop simulator
 *
 * Maps FreeRTOS tasks/queues/semaphores/timers to pthread primitives.
 */
#define _GNU_SOURCE
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
#include <unistd.h>

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

    /* Set thread name (Linux-specific, best-effort) */
#if defined(__linux__)
    pthread_setname_np(pthread_self(), tw->name);
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
