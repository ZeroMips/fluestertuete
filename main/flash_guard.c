#include "flash_guard.h"

#include "esp_memory_utils.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct {
    void (*fn)(void *);
    void *arg;
    TaskHandle_t caller;
} job_t;

static QueueHandle_t s_jobs;

static void helper_task(void *unused)
{
    job_t j;
    for (;;) {
        if (!xQueueReceive(s_jobs, &j, portMAX_DELAY)) continue;
        j.fn(j.arg);
        xTaskNotifyGive(j.caller);
    }
}

void flash_guard_init(void)
{
    s_jobs = xQueueCreate(4, sizeof(job_t));
    xTaskCreatePinnedToCore(helper_task, "flash", 4096, NULL, 6, NULL, 0);  // stack in internal RAM
}

void flash_run(void (*fn)(void *), void *arg)
{
    int here;
    if (esp_ptr_internal(&here) || !s_jobs) {
        fn(arg);
        return;
    }
    job_t j = {fn, arg, xTaskGetCurrentTaskHandle()};
    xQueueSend(s_jobs, &j, portMAX_DELAY);
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}
