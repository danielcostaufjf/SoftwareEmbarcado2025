#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>


void vTask1(void* pvparameters)
{
    ESP_LOGI("T1","Task inicializando...");

    while(1)
    {
        ESP_LOGI("T1","Task executando...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void vTask2(void* pvparameters)
{
    ESP_LOGW("T2","Task inicializando...");

    while(1)
    {
        ESP_LOGW("T2","Task executando...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}


void app_main(void)
{
    printf("Hello!!!\r\n");

    xTaskCreate(vTask1, "Task 1", 2048, NULL, 2, NULL);

    xTaskCreate(vTask2, "Task 2", 2048, NULL, 2, NULL);
}
