#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>
#include <driver/spi_master.h>

// ============================================================
// 学习闹钟 自定义板（ESP32-S3-N16R8）
// 硬件：3.5" ST7796U 电容屏（FT6336 触摸）+ INMP441 麦克风 + MAX98357 功放
// 引脚与 D:\storage\learning-alarm\src\main.cpp 逐根一致
// ============================================================

// ── 音频：INMP441 麦克风（收）+ MAX98357 功放（发），Simplex 双 I2S ──
#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000
#define AUDIO_I2S_METHOD_SIMPLEX

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_16   // 麦 WS
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_17   // 麦 SCK
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_18   // 麦 SD
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_21   // 功放 DIN
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_19   // 功放 BCLK
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_20   // 功放 LRC(WS)

// ── 触摸 I2C（FT6336，地址 0x38）──
#define TOUCH_I2C_SDA_PIN GPIO_NUM_8    // SDA
#define TOUCH_I2C_SCL_PIN GPIO_NUM_6    // SCL
#define TOUCH_RST_PIN     GPIO_NUM_4    // 触摸复位
#define TOUCH_INT_PIN     GPIO_NUM_5    // 触摸中断

// ── 屏幕 SPI（ST7796U 320x480 竖屏物理，横屏 480x320 显示）──
#define DISPLAY_SPI_MODE        0
#define DISPLAY_CS_PIN          GPIO_NUM_10
#define DISPLAY_MOSI_PIN        GPIO_NUM_11
#define DISPLAY_MISO_PIN        GPIO_NUM_13
#define DISPLAY_CLK_PIN         GPIO_NUM_12
#define DISPLAY_DC_PIN          GPIO_NUM_9
#define DISPLAY_RST_PIN         GPIO_NUM_14

#define DISPLAY_WIDTH           480
#define DISPLAY_HEIGHT          320
#define DISPLAY_MIRROR_X        false
#define DISPLAY_MIRROR_Y        false
#define DISPLAY_SWAP_XY         true
#define DISPLAY_RGB_ORDER       LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_OFFSET_X        0
#define DISPLAY_OFFSET_Y        0

#define DISPLAY_BACKLIGHT_PIN          GPIO_NUM_15
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// ── 板级 ──
#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_0

#endif // _BOARD_CONFIG_H_
