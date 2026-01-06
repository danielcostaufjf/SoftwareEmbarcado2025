#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "wifi.h"
#include "my_mqtt.h"
#include "dht11.h"
#include "ssd1306.h"
#include "driver/i2c.h"

#define BANCADA_ID "BancadaZ" 
#define MAX_WINDOW_SIZE 20
#define FLAME_SENSOR 17
#define GAS_SENSOR 19
#define TRUE 1
#define FALSE 0

// estrutura para enviar dados para a task de publicação MQTT
typedef struct {
    char topic[100];
    char payload[20];
} mqtt_message_t;

SSD1306_t dev;

SemaphoreHandle_t wificonnectedSemaphore;
SemaphoreHandle_t mqttconnectedSemaphore;
SemaphoreHandle_t dadosMutex;
SemaphoreHandle_t displayUpdateSemaphore;
MessageBufferHandle_t buffer_MQTT; // buffer para receber dados do broker 

QueueHandle_t fila_temp;   // Fila: Task Aquisição -> Task Média Temp
QueueHandle_t fila_hum;    // Fila: Task Aquisição -> Task Média Umi
QueueHandle_t fila_mqtt;   // Fila: Tasks Média -> Task MQTT

int N = 10; 
bool flame, gas;
struct dht11_reading ultima_leitura;
int mediaTemp = 0, mediaHum = 0;

void display_init(void) {
    i2c_master_init(&dev, CONFIG_SDA_GPIO, CONFIG_SCL_GPIO, CONFIG_RESET_GPIO);
    ssd1306_init(&dev, 128, 64);
    ssd1306_clear_screen(&dev, false);
    ssd1306_contrast(&dev, 0xff);
}

void vTaskDisplay(void *pvParameters) {
    char linha_temp[20];
    char linha_hum[20];
    
    struct dht11_reading leitura_local;
    char data_local[12];
    char hora_local[10];

    while(1) {
        xSemaphoreTake(displayUpdateSemaphore, portMAX_DELAY);
        ESP_LOGI("DISPLAY", "DISPLAY RODANDO");
        
        xSemaphoreTake(dadosMutex, portMAX_DELAY);
        leitura_local.temperature = mediaTemp;
        leitura_local.humidity = mediaHum;
        xSemaphoreGive(dadosMutex);
        
        snprintf(linha_temp, sizeof(linha_temp), "T:%d C  U:%d %%", leitura_local.temperature, leitura_local.humidity);

        ssd1306_clear_screen(&dev, false);
        ssd1306_display_text(&dev, 0, "Monitor UFJF", 12, false);
        ssd1306_display_text(&dev, 2, "linha_data", 12, false);
        ssd1306_display_text(&dev, 3, "linha_hora", 12, false);
        ssd1306_display_text(&dev, 5, linha_temp, strlen(linha_temp), false);
    }
}


void task_aquisicao(void *params)
{
    struct dht11_reading leitura;
    
    xSemaphoreTake(mqttconnectedSemaphore, portMAX_DELAY);
    xSemaphoreGive(mqttconnectedSemaphore);

    DHT11_init(DHT11_PIN);

    while (1)
    {
        leitura = DHT11_read();        

        if (leitura.status == DHT11_OK) {
            xQueueSend(fila_temp, &leitura.temperature, 0);
            xQueueSend(fila_hum, &leitura.humidity, 0);
            ESP_LOGI("AQUISICAO", "Leitura: T=%d, H=%d", leitura.temperature, leitura.humidity);
        } else {
            ESP_LOGE("AQUISICAO", "Erro leitura DHT11");
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_leitura_chamas(void *params)
{
    mqtt_message_t msg_to_send;
    while (1)
    {
        if (gpio_get_level(FLAME_SENSOR) == 1)
        {
            ESP_LOGW("CHAMAS", "FOGO");
            flame = TRUE;
        }
        else
        {
            flame = FALSE;
        }
        snprintf(msg_to_send.topic, sizeof(msg_to_send.topic), "CEL080B/Sensores/%s/CHAMA", BANCADA_ID);
        snprintf(msg_to_send.payload, sizeof(msg_to_send.payload), "%d", flame);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_leitura_gas(void *params)
{
    mqtt_message_t msg_to_send;
    while (1)
    {
        if (gpio_get_level(GAS_SENSOR) == 1)
        {
            ESP_LOGW("GAS", "GAS DETECTADO");
            gas = TRUE;
        }
        else 
        {
            gas = FALSE;
        }
        snprintf(msg_to_send.topic, sizeof(msg_to_send.topic), "CEL080B/Sensores/%s/GAS", BANCADA_ID);
        snprintf(msg_to_send.payload, sizeof(msg_to_send.payload), "%d", gas);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task_media_temp(void *params)
{
    int raw_temp;
    int medida[MAX_WINDOW_SIZE] = {0};
    int index = 0;
    int soma = 0;
    int count = 0; // Quantas amostras válidas temos até agora
    mqtt_message_t msg_to_send;

    while (1)
    {
        if (xQueueReceive(fila_temp, &raw_temp, portMAX_DELAY))
        {
            int atualN = (N > MAX_WINDOW_SIZE) ? MAX_WINDOW_SIZE : N;
            if (atualN < 1) atualN = 1;

            soma -= medida[index];       // Remove o valor antigo
            medida[index] = raw_temp;   // Adiciona novo valor
            soma += medida[index];       // Soma novo valor
            
            index = (index + 1) % atualN; // Avança indice circular
            if (count < atualN) count++; // Conta até encher a janela

            int media = soma / count;
            xSemaphoreTake(dadosMutex, portMAX_DELAY);
            mediaTemp = media;
            xSemaphoreGive(dadosMutex);

            snprintf(msg_to_send.topic, sizeof(msg_to_send.topic), "CEL080B/Sensores/%s/Temperatura", BANCADA_ID);
            snprintf(msg_to_send.payload, sizeof(msg_to_send.payload), "%d", media);

            xQueueSend(fila_mqtt, &msg_to_send, portMAX_DELAY);
        }
    }
}


void task_media_hum(void *params)
{
    int raw_hum;
    int medida[MAX_WINDOW_SIZE] = {0};
    int index = 0;
    int soma = 0;
    int count = 0;
    mqtt_message_t msg_to_send;

    while (1)
    {
        if (xQueueReceive(fila_hum, &raw_hum, portMAX_DELAY))
        {
            int atualN = (N > MAX_WINDOW_SIZE) ? MAX_WINDOW_SIZE : N;
            if (atualN < 1) atualN = 1;

            soma -= medida[index];
            medida[index] = raw_hum;
            soma += medida[index];
            
            index = (index + 1) % atualN;
            if (count < atualN) count++;

            int media = soma / count;
            xSemaphoreTake(dadosMutex, portMAX_DELAY);
            mediaHum = media;
            xSemaphoreGive(dadosMutex);

            snprintf(msg_to_send.topic, sizeof(msg_to_send.topic), "CEL080B/Sensores/%s/Umidade", BANCADA_ID);
            snprintf(msg_to_send.payload, sizeof(msg_to_send.payload), "%d", media);

            xQueueSend(fila_mqtt, &msg_to_send, portMAX_DELAY);
        }
    }
}

void task_mqtt_manager(void *params)
{
    mqtt_message_t received_msg;
    char rx_buffer[50]; 
    size_t rx_size;

    xSemaphoreTake(mqttconnectedSemaphore, portMAX_DELAY);
    mqtt_sbscribe("CEL080B/Sensores/TamanhoMedia");
    xSemaphoreGive(mqttconnectedSemaphore); 

    while (1)
    {
        if (xQueueReceive(fila_mqtt, &received_msg, 100 / portTICK_PERIOD_MS)) 
        {
            mqtt_publish(received_msg.topic, received_msg.payload);
            xSemaphoreGive(displayUpdateSemaphore);
        }

        rx_size = xMessageBufferReceive(buffer_MQTT, rx_buffer, sizeof(rx_buffer), 100 / portTICK_PERIOD_MS);
        if (rx_size > 0)
        {
            rx_buffer[rx_size] = '\0'; 
            int new_n = atoi(rx_buffer);
            if (new_n > 0 && new_n <= MAX_WINDOW_SIZE) {
                N = new_n;
                ESP_LOGI("MQTT_RX", "Novo tamanho de janela N: %d", N);
            }
        }
    }
}


void wifiConnected(void *params)
{
    while (1)
    {
        if (xSemaphoreTake(wificonnectedSemaphore, portMAX_DELAY))
        {
            mqtt_start();
        }
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    gpio_reset_pin(FLAME_SENSOR);
    gpio_set_direction(FLAME_SENSOR, GPIO_MODE_INPUT);
    gpio_reset_pin(GAS_SENSOR);
    gpio_set_direction(GAS_SENSOR, GPIO_MODE_INPUT);

    // Inicializa Semáforos e Buffers
    dadosMutex = xSemaphoreCreateMutex();
    wificonnectedSemaphore = xSemaphoreCreateBinary();
    mqttconnectedSemaphore = xSemaphoreCreateBinary();
    displayUpdateSemaphore = xSemaphoreCreateBinary();
    buffer_MQTT = xMessageBufferCreate(200);

    // Inicializa Filas
    fila_temp = xQueueCreate(10, sizeof(int));
    fila_hum = xQueueCreate(10, sizeof(int));
    fila_mqtt = xQueueCreate(10, sizeof(mqtt_message_t));

    display_init();
    wifi_start();

    // Criação das Tasks
    xTaskCreate(wifiConnected, "Conexao WiFi", 4096, NULL, 5, NULL);
    xTaskCreate(task_mqtt_manager, "Manager MQTT", 4096, NULL, 4, NULL);
    
    // Tasks de processamento
    xTaskCreate(task_aquisicao, "Aquisicao", 2048, NULL, 3, NULL);
    xTaskCreate(task_media_temp, "Media Temp", 2048, NULL, 3, NULL);
    xTaskCreate(task_media_hum, "Media Hum", 2048, NULL, 3, NULL);
    xTaskCreate(task_leitura_chamas, "Leitura Chama", 2048, NULL, 3, NULL);
    xTaskCreate(task_leitura_gas, "Leitura GAS", 2048, NULL, 3, NULL);
    xTaskCreate(vTaskDisplay, "TaskDisplay", 2048, NULL, 4, NULL);
}
