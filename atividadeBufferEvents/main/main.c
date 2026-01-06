#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "freertos/message_buffer.h"
#include "freertos/event_groups.h"

#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include <dht11.h>

#define MOVING_AVERAGE_WINDOW   10U
#define SENSOR_ACQ_PERIOD_MS    2000U
#define TEMP_STREAM_LENGTH      (sizeof(float) * 20U)
#define HUM_STREAM_LENGTH       (sizeof(float) * 20U)
#define AVG_MESSAGE_BUFFER_LEN  (sizeof(float) * 6U)
#define DISPLAY_TEMP_READY_BIT  BIT0
#define DISPLAY_HUM_READY_BIT   BIT1
#define READY_BITS              (DISPLAY_TEMP_READY_BIT | DISPLAY_HUM_READY_BIT)
#define TEMP_LIMIT_C            30.0f
#define HUM_LIMIT_PERCENT       70.0f
#ifndef CONFIG_APP_DHT11_GPIO
#define CONFIG_APP_DHT11_GPIO   DHT11_PIN
#endif

typedef struct {
    float temperature;
    float humidity;
} sensor_sample_t;

#define ALARM_MESSAGE_LENGTH    (sizeof(sensor_sample_t) * 6U)

typedef struct {
    float buffer[MOVING_AVERAGE_WINDOW];
    size_t index;
    size_t count;
    float sum;
} moving_average_t;

typedef struct {
    const char *name;
    StreamBufferHandle_t stream;
    MessageBufferHandle_t outbound;
    EventBits_t ready_bit;
} avg_task_params_t;

static StreamBufferHandle_t s_temp_stream = NULL;
static StreamBufferHandle_t s_humidity_stream = NULL;
static MessageBufferHandle_t s_temp_avg_msg = NULL;
static MessageBufferHandle_t s_humidity_avg_msg = NULL;
static MessageBufferHandle_t s_alarm_msg = NULL;
static EventGroupHandle_t s_display_event_group = NULL;

static avg_task_params_t s_temp_avg_params;
static avg_task_params_t s_humidity_avg_params;

static const char *TAG = "buffer_events";

static void moving_average_init(moving_average_t *avg)
{
    memset(avg, 0, sizeof(*avg));
}

static float moving_average_add(moving_average_t *avg, float value)
{
    if (avg->count == MOVING_AVERAGE_WINDOW) {
        avg->sum -= avg->buffer[avg->index];
    } else {
        avg->count++;
    }

    avg->buffer[avg->index] = value;
    avg->sum += value;
    avg->index = (avg->index + 1U) % MOVING_AVERAGE_WINDOW;

    return avg->sum / (float)avg->count;
}

static bool get_dht11_sample(sensor_sample_t *sample)
{
    struct dht11_reading reading = DHT11_read();
    if (reading.status != DHT11_OK) {
        ESP_LOGW(TAG, "Falha na leitura do DHT11 (status=%d)", reading.status);
        return false;
    }

    sample->temperature = (float)reading.temperature;
    sample->humidity = (float)reading.humidity;
    return true;
}

static void acquisition_task(void *param)
{
    const TickType_t delay_ticks = pdMS_TO_TICKS(SENSOR_ACQ_PERIOD_MS);

    while (true) {
        sensor_sample_t sample;

        if (get_dht11_sample(&sample)) {
            if (xStreamBufferSend(s_temp_stream, &sample.temperature, sizeof(sample.temperature), pdMS_TO_TICKS(50)) != sizeof(sample.temperature)) {
                ESP_LOGW(TAG, "Stream de temperatura cheio, amostra descartada");
            }

            if (xStreamBufferSend(s_humidity_stream, &sample.humidity, sizeof(sample.humidity), pdMS_TO_TICKS(50)) != sizeof(sample.humidity)) {
                ESP_LOGW(TAG, "Stream de umidade cheio, amostra descartada");
            }

            if (xMessageBufferSend(s_alarm_msg, &sample, sizeof(sample), pdMS_TO_TICKS(10)) != sizeof(sample)) {
                ESP_LOGW(TAG, "Buffer de alarme cheio, amostra descartada");
            }
        }

        vTaskDelay(delay_ticks);
    }
}

static void moving_average_task(void *param)
{
    avg_task_params_t *params = (avg_task_params_t *)param;
    moving_average_t avg;
    moving_average_init(&avg);
    float sample = 0.0f;

    while (true) {
        size_t received = xStreamBufferReceive(params->stream, &sample, sizeof(sample), portMAX_DELAY);
        if (received != sizeof(sample)) {
            continue;
        }

        float avg_value = moving_average_add(&avg, sample);
        if (avg.count < MOVING_AVERAGE_WINDOW) {
            continue;
        }

        if (xMessageBufferSend(params->outbound, &avg_value, sizeof(avg_value), pdMS_TO_TICKS(20)) == sizeof(avg_value)) {
            xEventGroupSetBits(s_display_event_group, params->ready_bit);
        } else {
            ESP_LOGW(TAG, "Display (%s) ocupado, media descartada", params->name);
        }
    }
}

static void display_task(void *param)
{
    float temp_avg = 0.0f;
    float hum_avg = 0.0f;

    while (true) {
        EventBits_t bits = xEventGroupWaitBits(s_display_event_group, READY_BITS, pdTRUE, pdTRUE, portMAX_DELAY);
        if ((bits & READY_BITS) != READY_BITS) {
            continue;
        }

        if (xMessageBufferReceive(s_temp_avg_msg, &temp_avg, sizeof(temp_avg), portMAX_DELAY) != sizeof(temp_avg)) {
            continue;
        }

        if (xMessageBufferReceive(s_humidity_avg_msg, &hum_avg, sizeof(hum_avg), portMAX_DELAY) != sizeof(hum_avg)) {
            continue;
        }

        printf("Temperatura media: %.2f C | Umidade media: %.2f %%\n", temp_avg, hum_avg);
        fflush(stdout);
    }
}

static void alarm_task(void *param)
{
    sensor_sample_t sample;

    while (true) {
        size_t bytes = xMessageBufferReceive(s_alarm_msg, &sample, sizeof(sample), portMAX_DELAY);
        if (bytes != sizeof(sample)) {
            continue;
        }

        if (sample.temperature > TEMP_LIMIT_C || sample.humidity > HUM_LIMIT_PERCENT) {
            ESP_LOGW(TAG, "ALARME: T=%.2f C (lim %.2f) | H=%.2f %% (lim %.2f)", sample.temperature, TEMP_LIMIT_C, sample.humidity, HUM_LIMIT_PERCENT);
        }
    }
}

static bool create_runtime_objects(void)
{
    s_temp_stream = xStreamBufferCreate(TEMP_STREAM_LENGTH, sizeof(float));
    s_humidity_stream = xStreamBufferCreate(HUM_STREAM_LENGTH, sizeof(float));
    s_temp_avg_msg = xMessageBufferCreate(AVG_MESSAGE_BUFFER_LEN);
    s_humidity_avg_msg = xMessageBufferCreate(AVG_MESSAGE_BUFFER_LEN);
    s_alarm_msg = xMessageBufferCreate(ALARM_MESSAGE_LENGTH);
    s_display_event_group = xEventGroupCreate();

    return s_temp_stream && s_humidity_stream && s_temp_avg_msg && s_humidity_avg_msg && s_alarm_msg && s_display_event_group;
}

void app_main(void)
{
    DHT11_init(CONFIG_APP_DHT11_GPIO);

    if (!create_runtime_objects()) {
        ESP_LOGE(TAG, "Falha ao criar objetos do kernel");
        return;
    }

    s_temp_avg_params = (avg_task_params_t) {
        .name = "temperatura",
        .stream = s_temp_stream,
        .outbound = s_temp_avg_msg,
        .ready_bit = DISPLAY_TEMP_READY_BIT,
    };

    s_humidity_avg_params = (avg_task_params_t) {
        .name = "umidade",
        .stream = s_humidity_stream,
        .outbound = s_humidity_avg_msg,
        .ready_bit = DISPLAY_HUM_READY_BIT,
    };

    xTaskCreate(acquisition_task, "acquisition", 4096, NULL, 5, NULL);
    xTaskCreate(moving_average_task, "avg_temp", 4096, &s_temp_avg_params, 4, NULL);
    xTaskCreate(moving_average_task, "avg_hum", 4096, &s_humidity_avg_params, 4, NULL);
    xTaskCreate(display_task, "display", 4096, NULL, 3, NULL);
    xTaskCreate(alarm_task, "alarm", 4096, NULL, 3, NULL);
}
