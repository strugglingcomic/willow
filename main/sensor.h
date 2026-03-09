#pragma once

#include "esp_err.h"

#define AHT30_ADDR     0x38         // 7-bit I2C address (for i2c_bus_probe_addr)
#define AHT30_ADDR_RW  (0x38 << 1)  // 8-bit shifted (for i2c_bus read/write)

typedef struct {
    float temperature; // Celsius
    float humidity;    // Percent
    bool valid;
} sensor_reading_t;

esp_err_t init_sensor(void);
