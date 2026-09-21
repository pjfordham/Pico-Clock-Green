#ifndef _MQTT_H
#define _MQTT_H

void mqtt_init();
void mqtt_run();
void mqtt_send(char *topic, char *payload);
void mqtt_send_float(char *topic, float payload);

#endif
