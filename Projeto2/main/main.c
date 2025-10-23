#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>


TaskHandle_t handleT1, handleT2;


void vTask1(void *pvparameters)
{
    portBASE_TYPE coreID;
    uint16_t ii, jj;

    ESP_LOGI("T1", "Task inicializando....");
    
    while(1)
    {
        coreID = xPortGetCoreID();

        ESP_LOGI("T1", "Task executando no core = %d.",coreID);

        for(ii = 0; ii <100; ii++)
        {
            for(jj = 0; jj < 50000; jj++)
            {
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


void vTask2(void *pvparameters)
{
    portBASE_TYPE coreID;
    uint16_t ii, jj;

    ESP_LOGI("T2", "Task inicializando....");
    
    while(1)
    {
        coreID = xPortGetCoreID();

        ESP_LOGI("T2", "Task executando no core = %d.",coreID);

        for(ii = 0; ii <100; ii++)
        {
            for(jj = 0; jj < 50000; jj++)
            {
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


void app_main(void)
{
    vTaskPrioritySet(NULL, 3);

    xTaskCreatePinnedToCore(vTask1, "Task 1", 2048, NULL, 2, &handleT1, 0);
    ESP_LOGI("INIT","Task1 foi criada!");

    xTaskCreatePinnedToCore(vTask2, "Task 2", 2048, NULL, 2, &handleT2, 1);
    ESP_LOGI("INIT","Task2 foi criada!");
}
