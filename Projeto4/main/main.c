#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <dht11.h>


struct dht11_reading leitura;


void vTaskLeitura(void *pvparameters)
{
    ESP_LOGI("LEITURA","Task inicializando...");

    while (1)
    {
        leitura = DHT11_read();
        ESP_LOGI("Leitura","Temp = %d e Umid = %d",leitura.temperature, leitura.humidity);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
}

void app_main(void)
{
    // Incialização dos Periféricos
    DHT11_init(DHT11_PIN);

    //Cria as tasks
    xTaskCreate(vTaskLeitura,"LT", 2048, NULL, 2, NULL);
}
