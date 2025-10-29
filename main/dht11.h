#ifndef _DHT11_H
#define _DHT11_H

#include "main.h"

#define DHT11_GPIO 4

extern int16_t humidity;  // Dùng int16_t thay cho float
extern int16_t temperature;  // Dùng int16_t thay cho float

void dht11_read_task(void *pvParameter);


#endif