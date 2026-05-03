/*
 * FreeRTOS FreeRTOS.h stub for desktop simulator
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "freertos/portmacro.h"
#include "freertos/projdefs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- Base types --- */
typedef void (*TaskFunction_t)(void *);
typedef void * TaskHandle_t;
typedef TaskHandle_t QueueHandle_t;
typedef TaskHandle_t SemaphoreHandle_t;
typedef int32_t  BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t StackType_t;
typedef uint32_t UBaseType_t;

#define tskIDLE_PRIORITY 0
#define tskNO_AFFINITY   (-1)

#define portMAX_DELAY    UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTICKS_TO_MS(t)  ((TickType_t)(t))

#define configTICK_RATE_HZ 1000

/* Inline helpers for ISR context (always false on desktop) */
static inline int xPortInIsrContext(void) { return 0; }
static inline void portYIELD_FROM_ISR(void) {}

/* xTaskCreateWithCaps — same as xTaskCreate but with stack caps (ignored on desktop) */
BaseType_t xTaskCreateWithCaps(TaskFunction_t task_func,
                                const char *name,
                                uint32_t stack_size,
                                void *arg,
                                UBaseType_t priority,
                                TaskHandle_t *task_handle,
                                UBaseType_t caps);

/* Event group bit masks (used by gfx/emote) */
#define BIT0   (1 << 0)
#define BIT1   (1 << 1)
#define BIT2   (1 << 2)
#define BIT3   (1 << 3)
#define BIT4   (1 << 4)
#define BIT5   (1 << 5)
#define BIT6   (1 << 6)
#define BIT7   (1 << 7)
#define BIT8   (1 << 8)
#define BIT9   (1 << 9)
#define BIT10  (1 << 10)
#define BIT11  (1 << 11)
#define BIT12  (1 << 12)
#define BIT13  (1 << 13)
#define BIT14  (1 << 14)
#define BIT15  (1 << 15)
#define BIT16  (1 << 16)
#define BIT17  (1 << 17)
#define BIT18  (1 << 18)
#define BIT19  (1 << 19)
#define BIT20  (1 << 20)
#define BIT21  (1 << 21)
#define BIT22  (1 << 22)
#define BIT23  (1 << 23)
#define BIT24  (1 << 24)
#define BIT25  (1 << 25)
#define BIT26  (1 << 26)
#define BIT27  (1 << 27)
#define BIT28  (1 << 28)
#define BIT29  (1 << 29)
#define BIT30  (1 << 30)
#define BIT31  (1u << 31)

/* --- Task API --- */

BaseType_t xTaskCreate(TaskFunction_t task_func,
                       const char *name,
                       uint32_t stack_size,
                       void *arg,
                       UBaseType_t priority,
                       TaskHandle_t *task_handle);

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t task_func,
                                   const char *name,
                                   uint32_t stack_size,
                                   void *arg,
                                   UBaseType_t priority,
                                   TaskHandle_t *task_handle,
                                   BaseType_t core_id);

BaseType_t xTaskCreatePinnedToCoreWithCaps(TaskFunction_t task_func,
                                           const char *name,
                                           uint32_t stack_size,
                                           void *arg,
                                           UBaseType_t priority,
                                           TaskHandle_t *task_handle,
                                           BaseType_t core_id,
                                           UBaseType_t caps);

void vTaskDelete(TaskHandle_t task_handle);
void vTaskDeleteWithCaps(TaskHandle_t task_handle);

/* On desktop, withCaps variants are identical to base versions */
#define vTaskDeleteWithCaps(handle) vTaskDelete(handle)

void vTaskDelay(TickType_t ticks);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);

/* --- Queue API --- */

QueueHandle_t xQueueCreate(UBaseType_t queue_length, UBaseType_t item_size);
BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t ticks_to_wait);
BaseType_t xQueueReceive(QueueHandle_t queue, void *buffer, TickType_t ticks_to_wait);
void vQueueDelete(QueueHandle_t queue);

/* --- Semaphore API --- */

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks_to_wait);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t sem, TickType_t ticks_to_wait);
BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t sem);
void vSemaphoreDelete(SemaphoreHandle_t sem);

/* --- Task notification (minimal stub) --- */
BaseType_t xTaskNotifyGive(TaskHandle_t task);
BaseType_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks_to_wait);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);

/* --- Event Group API --- */
typedef void *EventGroupHandle_t;
typedef uint32_t EventBits_t;

EventGroupHandle_t xEventGroupCreate(void);
void vEventGroupDelete(EventGroupHandle_t eg);
EventBits_t xEventGroupSetBits(EventGroupHandle_t eg, EventBits_t bits);
EventBits_t xEventGroupClearBits(EventGroupHandle_t eg, EventBits_t bits);
EventBits_t xEventGroupGetBits(EventGroupHandle_t eg);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t eg,
                                EventBits_t bits_to_wait_for,
                                BaseType_t clear_on_exit,
                                BaseType_t wait_for_all_bits,
                                TickType_t ticks_to_wait);
BaseType_t xEventGroupSetBitsFromISR(EventGroupHandle_t eg, EventBits_t bits,
                                      BaseType_t *pxHigherPriorityTaskWoken);

#ifdef __cplusplus
}
#endif
