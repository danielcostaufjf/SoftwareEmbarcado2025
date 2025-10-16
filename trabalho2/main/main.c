#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <dht11.h>
#include <driver/gpio.h>


void taskSensor(void* pvParameters);
void taskDisplay(void* pvParameters);


struct dht11_reading data;

SemaphoreHandle_t sem_mutex;
SemaphoreHandle_t sem_bin;


void taskSensor(void* pvParameters)
{
    BaseType_t status;

    ESP_LOGI("Sensor","Task Sensor inicializando");
    while(1)
    {
        status = xSemaphoreTake(sem_bin, portMAX_DELAY);
        if(status == pdTRUE)
        {
            xSemaphoreTake(sem_mutex, portMAX_DELAY);
            data = DHT11_read();
            xSemaphoreGive(sem_mutex);
            xSemaphoreGive(sem_bin);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


void taskDisplay(void* pvParameters)
{
    ESP_LOGI("Display","Task Display inicializando");
    while(1)
    {
        xSemaphoreTake(sem_bin, portMAX_DELAY);
        xSemaphoreTake(sem_mutex, portMAX_DELAY);
        ESP_LOGI("Display", "Temperatura: %d , Umidade: %d", data.temperature, data.humidity);
        xSemaphoreGive(sem_mutex);
    }   
}


void app_main(void)
{
    DHT11_init(DHT11_PIN);

    sem_mutex = xSemaphoreCreateMutex();
    sem_bin = xSemaphoreCreateBinary();
    xSemaphoreGive(sem_bin);

    xTaskCreatePinnedToCore(taskSensor, "Sensor", 2048, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(taskDisplay, "Display", 2048, NULL, 2, NULL, 0);
}
