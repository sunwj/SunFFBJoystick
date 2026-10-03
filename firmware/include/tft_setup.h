/**
 * Project ST7735 80x160 setup; application rotation produces the 160x80 landscape view.
 * The S3 LCD environment force-includes this file for both firmware and TFT_eSPI translation units.
 * USER_SETUP_LOADED bypasses the library User_Setup.h; keep one effective configuration.
 */

#ifndef TFT_ESPI_USER_SETUP_H
#define TFT_ESPI_USER_SETUP_H

// Bypass the library User_Setup.h; this file is shared by all translation units.
#define USER_SETUP_LOADED
#define USER_SETUP_INFO "SunFFB ESP32-S3 ST7735 80x160"

#define ST7735_DRIVER
#define TFT_WIDTH 80
#define TFT_HEIGHT 160
#define ST7735_GREENTAB160x80
#define TFT_RGB_ORDER TFT_BGR
#define TFT_INVERSION_ON

#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS 39
#define TFT_DC 40
#define TFT_RST 38
#define TFT_BL 41
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
// Font 1 is used by the debug UI; the remaining font switches preserve the supplied board setup.
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define USE_HSPI_PORT
// Keep the supplied HSPI selection and clock rates; changing buses can break otherwise correct pin mapping.
#define SPI_FREQUENCY 27000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000

#endif
