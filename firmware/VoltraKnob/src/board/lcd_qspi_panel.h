#pragma once
/*
 * QSPI panel driver for the ST77916 used on the Waveshare
 * ESP32-S3-Knob-Touch-LCD-1.8. Derived from the Espressif esp_lcd_sh8601
 * component (Apache-2.0) with the Waveshare ST77916 initialisation table baked in.
 */
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create a 16-bit RGB565 QSPI panel on the given panel IO.
 * @param io              panel IO created with quad_mode = true, lcd_cmd_bits = 32
 * @param reset_gpio_num  hardware reset GPIO or -1
 * @param ret_panel       resulting panel handle
 */
esp_err_t lcd_qspi_panel_create(esp_lcd_panel_io_handle_t io, int reset_gpio_num, esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
