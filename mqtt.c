#include "mqtt.h"
#include <string.h>
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "pico/multicore.h"


#define MQTT_BUFFER_SIZE 16
#define MQTT_TOPIC_SIZE 128
#define MQTT_PAYLOAD_SIZE 32

typedef struct {
   char topic[MQTT_TOPIC_SIZE];
   char payload[MQTT_PAYLOAD_SIZE];
} MqttMessage;


typedef struct {
   MqttMessage entries[MQTT_BUFFER_SIZE];
   size_t head;
   size_t tail;
   size_t count;
   mutex_t mutex;
} MqttBuffer;

mqtt_client_t *client = NULL;
int mqtt_up = 0;

bool mqtt_buffer_get(MqttMessage *message);
void mqtt_buffer_init(void);
void mqtt_connection_cb(mqtt_client_t *client, void *arg,
                        mqtt_connection_status_t status);
void mqtt_init() {
   ip_addr_t broker_addr;
   client = mqtt_client_new();
   ip4addr_aton("192.168.0.4", &broker_addr);

   struct mqtt_connect_client_info_t connect_params = {
      .client_id = "pico-clock-green",
      .client_user = "pico",
      .client_pass = "pico",
      .keep_alive = 60,
      .will_topic = NULL,
      .will_msg = NULL,
      .will_qos = 0,
      .will_retain = 0};

   // Should do this before we start core0, but it's probably fine
   mqtt_buffer_init();
   cyw43_arch_lwip_begin();
   mqtt_client_connect(client, &broker_addr, 1883, mqtt_connection_cb, NULL,
                       &connect_params);
   cyw43_arch_lwip_end();
}


void    mqtt_publish_cb(void *arg, err_t err)
{
   if (err == ERR_OK) {
      printf("MQTT publish OK\n");
   } else {
      printf("MQTT publish failed: %d\n", err);
   }
}

void mqtt_connection_cb(mqtt_client_t *client,
                        void *arg,
                        mqtt_connection_status_t status)
{
   if (status != MQTT_CONNECT_ACCEPTED) {
      printf("MQTT connection failed: %d\n", status);
      return;
   }

   printf("MQTT connected\n");


static const char *adc_temp_config =
   "{"
   "\"name\":\"Core Temperature\","
   "\"unique_id\":\"pico_clock_green_adc_temp\","
   "\"state_topic\":\"home/pico/core_temperature\","
   "\"device_class\":\"voltage\","
   "\"state_class\":\"measurement\","
   "\"unit_of_measurement\":\"V\","
   "\"device\":{"
   "\"identifiers\":[\"pico_clock_green\"],"
   "\"name\":\"Pico Clock Green\","
   "\"manufacturer\":\"Raspberry Pi\","
   "\"model\":\"Pico W\""
   "}"
   "}";

static const char *adc_light_config =
   "{"
   "\"name\":\"Ambient Light\","
   "\"unique_id\":\"pico_clock_green_adc_light\","
   "\"state_topic\":\"home/pico/ambient_light\","
   "\"device_class\":\"voltage\","
   "\"state_class\":\"measurement\","
   "\"unit_of_measurement\":\"V\","
   "\"device\":{"
   "\"identifiers\":[\"pico_clock_green\"],"
   "\"name\":\"Pico Clock Green\","
   "\"manufacturer\":\"Raspberry Pi\","
   "\"model\":\"Pico W\""
   "}"
   "}";


mqtt_publish(client,
   "homeassistant/sensor/pico_adc_temp/config",
   adc_temp_config, strlen(adc_temp_config),
   1, 1, mqtt_publish_cb, NULL);

mqtt_publish(client,
   "homeassistant/sensor/pico_adc_light/config",
   adc_light_config, strlen(adc_light_config),
   1, 1, mqtt_publish_cb, NULL);



   mqtt_up = 1;
}

void mqtt_run() {
   if (mqtt_up) {
      MqttMessage message;

      if (mqtt_buffer_get(&message)) {
         cyw43_arch_lwip_begin();
         mqtt_publish(client,
                      message.topic,
                      message.payload,
                      strlen(message.payload),
                      0, 0,
                      mqtt_publish_cb,
                      NULL);
         cyw43_arch_lwip_end();
      }
   }



}


MqttBuffer mqtt_buffer;
void mqtt_buffer_init(void)
{
   mutex_init(&mqtt_buffer.mutex);
   mqtt_buffer.head = 0;
   mqtt_buffer.tail = 0;
   mqtt_buffer.count = 0;
}

void mqtt_send(char *topic, char *payload) {
   mutex_enter_blocking(&mqtt_buffer.mutex);
   strncpy(mqtt_buffer.entries[mqtt_buffer.head].topic,
           topic, MQTT_TOPIC_SIZE - 1);
   mqtt_buffer.entries[mqtt_buffer.head].topic[MQTT_TOPIC_SIZE - 1] = '\0';

   strncpy(mqtt_buffer.entries[mqtt_buffer.head].payload,
           payload, MQTT_PAYLOAD_SIZE - 1);
   mqtt_buffer.entries[mqtt_buffer.head].payload[MQTT_PAYLOAD_SIZE - 1] = '\0';

   mqtt_buffer.head = (mqtt_buffer.head + 1) % MQTT_BUFFER_SIZE;

   if (mqtt_buffer.count < MQTT_BUFFER_SIZE)
      mqtt_buffer.count++;
   mutex_exit(&mqtt_buffer.mutex);

}

void mqtt_send_float(char *topic, float payload) {
   char BUFFER[80];
   snprintf(BUFFER, 79, "%2.2f", payload);
   printf("%s\n", BUFFER);
   mqtt_send(topic, BUFFER);
}


void mqtt_send_int(char *topic, int payload) {
   char BUFFER[80];
   snprintf(BUFFER, 79, "%d", payload);
   printf("%s\n", BUFFER);
   mqtt_send(topic, BUFFER);
}


bool mqtt_buffer_get(MqttMessage *message)
{
   bool available = false;

   mutex_enter_blocking(&mqtt_buffer.mutex);

   if (mqtt_buffer.count > 0) {
      *message = mqtt_buffer.entries[mqtt_buffer.tail];

      mqtt_buffer.tail = (mqtt_buffer.tail + 1) % MQTT_BUFFER_SIZE;
      mqtt_buffer.count--;

      available = true;
   }

   mutex_exit(&mqtt_buffer.mutex);

   return available;
}
