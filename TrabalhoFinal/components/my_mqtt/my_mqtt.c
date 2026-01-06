#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/message_buffer.h"

#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "my_mqtt.h"

static const char *TAG = "MQTT";

extern SemaphoreHandle_t mqttconnectedSemaphore;
extern MessageBufferHandle_t buffer_MQTT; 
extern const uint8_t ca_cert_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_cert_end[]   asm("_binary_ca_pem_end");

esp_mqtt_client_handle_t client;

static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0)
    {
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client = event->client; 
    
    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        xSemaphoreGive(mqttconnectedSemaphore);
        break;
        
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        
        // envia o dado recebido (novo valor de N) para o buffer correto
        xMessageBufferSend(buffer_MQTT, event->data, event->data_len, portMAX_DELAY);
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
        }
        break;
        
    default:
        ESP_LOGI(TAG, "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_start()
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtts://807f8dea1571445bb9e27e872bf01e96.s1.eu.hivemq.cloud:8883",
        .credentials.username = "esp32",
        .credentials.authentication.password = "Abc12345&",
        
        // --- CORREÇÃO AQUI ---
        // O campo correto é 'data', não 'certificate_pem'
        .broker.verification.certificate = (const char *)ca_cert_start,
        
        // É boa prática informar o tamanho para evitar erros de leitura de memória
        .broker.verification.certificate_len = ca_cert_end - ca_cert_start,
        
        .broker.verification.skip_cert_common_name_check = false,
        .broker.verification.use_global_ca_store = false,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
}


void mqtt_publish(char *topic, char *msg)
{
    int msg_id = esp_mqtt_client_publish(client, topic, msg, 0, 1, 0);
    ESP_LOGI(TAG, "Mensagem Enviada! ID = %d, Topic: %s, Msg: %s", msg_id, topic, msg);
}

void mqtt_sbscribe(char *topic)
{
    int msg_id = esp_mqtt_client_subscribe(client, topic, 0);
    ESP_LOGI(TAG, "Subscrito no Tópico %s! ID = %d", topic, msg_id);
}