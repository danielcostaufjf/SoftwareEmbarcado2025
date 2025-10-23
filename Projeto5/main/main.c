#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <driver/gpio.h>
#include <esp_log.h>


#define BT1 16
#define BT2 17
#define LD1 18
#define LD2 19

QueueHandle_t fila;

struct data{
    uint16_t cnt;
    uint8_t ID;
};


void vTaskBT1(void *pvparameters)
{
    struct data data1;
    data1.ID = 1;
    data1.cnt = 0;
    ESP_LOGI("BT1","Task inicializando...");

    while (1)
    {
        if(gpio_get_level(BT1) == 0)
        {
            data1.cnt++;
            ESP_LOGI("BT1","Valor a ser enviado = %d.",data1.cnt);
            xQueueSendToBack(fila, &data1, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }  
}


void vTaskBT2(void *pvparameters)
{
    struct data data2;
    data2.ID = 2;
    data2.cnt = 0;
    ESP_LOGI("BT2","Task inicializando...");

    while (1)
    {
        if(gpio_get_level(BT2) == 0)
        {
            data2.cnt++;
            ESP_LOGI("BT2","Valor a ser enviado = %d.",data2.cnt);
            xQueueSendToBack(fila, &data2, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }  
}


void vTaskDest(void *pvparameters)
{
    struct data rec_data;

    ESP_LOGI("DEST","Task inicializando...");
    while (1)
    {
        xQueueReceive(fila, &rec_data, portMAX_DELAY);
        ESP_LOGI("DEST","Dado recebido da BT%d = %d",rec_data.ID, rec_data.cnt);
        if(rec_data.ID == 1)
        {
            gpio_set_level(LD1, rec_data.cnt%2);
        }
        else
        {
            gpio_set_level(LD2, rec_data.cnt%2);
        }
    }    
}


void app_main(void)
{
    struct data fool;

    // Configurar periféricos
    gpio_reset_pin(BT1);
    gpio_set_direction(BT1, GPIO_MODE_INPUT);
    gpio_pullup_en(BT1);

    gpio_reset_pin(BT2);
    gpio_set_direction(BT2, GPIO_MODE_INPUT);
    gpio_pullup_en(BT2);

    gpio_reset_pin(LD1);
    gpio_set_direction(LD1, GPIO_MODE_OUTPUT);
    gpio_set_level(LD1, 1);

    gpio_reset_pin(LD2);
    gpio_set_direction(LD2, GPIO_MODE_OUTPUT);
    gpio_set_level(LD2, 1);
    
    // Criar objetos
    fila = xQueueCreate(10, sizeof(fool));

    // Criar as tarefas 
    xTaskCreate(vTaskBT1, "Task BT1", 2048, NULL, 2, NULL);
    xTaskCreate(vTaskBT2, "Task BT2", 2048, NULL, 2, NULL);
    xTaskCreate(vTaskDest, "Task DEST", 2048, NULL, 2, NULL);
}
