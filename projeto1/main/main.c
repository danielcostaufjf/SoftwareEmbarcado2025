#include <stdio.h>
#include <freertos\FreeRTOS.h>
#include <freertos\task.h>
#include <esp_log.h>

void vTask1(void *pvparameters);
void vTask2(void *pvparameters);


void vTask1(void *pvparameters)
{
    ESP_LOGI("Task1", "Task 1 inicializando");
    while(1)
    {
        ESP_LOGI("Task1", "Task 1 executando");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void vTask2(void *pvparameters)
{
    ESP_LOGI("Task2", "Task 2 inicializando");
    while(1)
    {
        ESP_LOGI("Task2", "Task 2 executando");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    xTaskCreate(vTask1, "Task 1", 4096, NULL, 1, NULL);
    xTaskCreate(vTask2, "Task 2", 4096, NULL, 1, NULL);
}