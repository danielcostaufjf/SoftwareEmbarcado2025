#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/gpio.h>
#include <esp_log.h>


#define LDIO 19
#define BTIO 22

uint16_t cnt = 0;

SemaphoreHandle_t sem_bin;
SemaphoreHandle_t mutex;


void vTaskS(void *pvparameters)
{
    ESP_LOGI("S","Task inicializando...");
    while (1)
    {
        if(gpio_get_level(BTIO) == 0)
        {
            xSemaphoreTake(mutex, portMAX_DELAY);
            cnt++;
            ESP_LOGI("S","Botao pressionado %d vezes!",cnt);
            xSemaphoreGive(mutex);
            
            xSemaphoreGive(sem_bin);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


void vTaskW(void *pvparameters)
{
    portBASE_TYPE status;

    ESP_LOGI("W","Task inicializando...");
    while (1)
    {
        ESP_LOGI("W","Tentando adquirir o semaforo...");
        status = xSemaphoreTake(sem_bin,pdMS_TO_TICKS(3000));
        
        if(status == pdTRUE)
        {
            xSemaphoreTake(mutex, portMAX_DELAY);
            ESP_LOGI("W","Conseguiu adquirir o semaforo! CNT = %d",cnt);
            gpio_set_level(LDIO,(cnt%2));   
            xSemaphoreGive(mutex);
        }
        else
        {
            ESP_LOGE("W","Nao foi possivel adquirir o semaforo");
        }
    }
}
    

void app_main(void)
{
    // 1 - Configurar os Periféricos
    gpio_reset_pin(BTIO);
    gpio_set_direction(BTIO, GPIO_MODE_INPUT);
    gpio_pullup_en(BTIO);

    gpio_reset_pin(LDIO);
    gpio_set_direction(LDIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LDIO, 1);

    // 2 - Declarar e inicializar os objetos do kernel
    vSemaphoreCreateBinary(sem_bin);
    xSemaphoreTake(sem_bin, 0);

    mutex = xSemaphoreCreateMutex();

    // 3 - Declarar as TASKS
    xTaskCreate(vTaskS, "S", 2048, NULL, 2, NULL);
    xTaskCreate(vTaskW, "W", 2048, NULL, 2, NULL);
}
