#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "font5x7.h"

// 1. กำหนดขาเชื่อมต่อตามแผนภาพวงจรจริง (GPIO 18, 23, 4, 2, 5)
#define OLED_PIN_SCK    (GPIO_NUM_18) // D0 (SPI Clock)
#define OLED_PIN_MOSI   (GPIO_NUM_23) // D1 (SPI MOSI Data)
#define OLED_PIN_RES    (GPIO_NUM_4)  // RES (Hardware Reset)
#define OLED_PIN_DC     (GPIO_NUM_2)  // DC (0 = Command, 1 = Data)
#define OLED_PIN_CS     (GPIO_NUM_5)  // CS (Chip Select - Active LOW)
#define POT_ADC_CHANNEL (ADC_CHANNEL_6) // GPIO 34 on ADC1
#define SERIAL_UART     (UART_NUM_0)    // USB-UART console connection

static spi_device_handle_t s_spi_handle = NULL;

// 2. ฟังก์ชันกำหนดค่าเริ่มต้นพิน GPIO และบัสฮาร์ดแวร์ SPI2
esp_err_t oled_spi_init(void)
{
    // กำหนดขา DC และ RES เป็น Output
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << OLED_PIN_DC) | (1ULL << OLED_PIN_RES),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    // กำหนดค่าบัส SPI (Master Out Only - ไม่ใช้ MISO)
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,               // จอนี้ Write-Only ไม่มีขา MISO
        .mosi_io_num = OLED_PIN_MOSI,     // GPIO 23
        .sclk_io_num = OLED_PIN_SCK,      // GPIO 18
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 1024 + 16,
    };

    // ใช้ SPI2_HOST (VSPI บน ESP32)
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return ret;

    // ผูก Device เข้ากับ Bus (ความถี่ 10 MHz, SPI Mode 0)
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz แสดงผลลื่นไหล
        .mode = 0,                          // Mode 0: CPOL=0, CPHA=0
        .spics_io_num = OLED_PIN_CS,        // GPIO 5
        .queue_size = 7,
    };

    return spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_handle);
}

// 3. ฟังก์ชันส่งคำสั่ง 1 ไบต์ (Command: DC = 0)
void oled_send_cmd(uint8_t cmd)
{
    gpio_set_level(OLED_PIN_DC, 0); // ดึง LOW เพื่อบอกชิปว่าเป็นคำสั่ง
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8; // 8 บิต (1 ไบต์)
    t.tx_buffer = &cmd;
    spi_device_polling_transmit(s_spi_handle, &t);
}

// 4. ฟังก์ชันส่งบล็อกข้อมูลพิกเซล (Data: DC = 1)
void oled_send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return;
    gpio_set_level(OLED_PIN_DC, 1); // ดึง HIGH เพื่อบอกชิปว่าเป็นข้อมูลพิกเซล
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8; // จำนวนบิต
    t.tx_buffer = data;
    spi_device_polling_transmit(s_spi_handle, &t);
}

static uint8_t s_oled_buffer[1024];

void oled_clear(void)
{
    memset(s_oled_buffer, 0x00, sizeof(s_oled_buffer));
}

void oled_draw_pixel(int x, int y, bool color)
{
    if (x < 0 || x >= 128 || y < 0 || y >= 64) {
        return;
    }

    int byte_index = x + (y / 8) * 128;
    int bit_offset = y % 8;

    if (color) {
        s_oled_buffer[byte_index] |= (1 << bit_offset);
    } else {
        s_oled_buffer[byte_index] &= ~(1 << bit_offset);
    }
}

void oled_flush(void)
{
    oled_send_cmd(0x21); // Set Column Address
    oled_send_cmd(0x00); // Start Column = 0
    oled_send_cmd(0x7F); // End Column = 127

    oled_send_cmd(0x22); // Set Page Address
    oled_send_cmd(0x00); // Start Page = 0
    oled_send_cmd(0x07); // End Page = 7

    oled_send_data(s_oled_buffer, sizeof(s_oled_buffer));
}

void oled_draw_char(int x, int y, char c, bool color)
{
    if (c < 32 || c > 126) {
        c = '?';
    }

    int font_idx = c - 32;

    for (int col = 0; col < 5; col++) {
        uint8_t line = font5x7[font_idx][col];
        for (int row = 0; row < 7; row++) {
            bool pixel = (line & (1 << row)) ? true : false;
            oled_draw_pixel(x + col, y + row, pixel == color);
        }
    }

    for (int row = 0; row < 7; row++) {
        oled_draw_pixel(x + 5, y + row, !color);
    }
}

void oled_draw_string(int x, int y, const char *str, bool color)
{
    while (*str) {
        oled_draw_char(x, y, *str, color);
        x += 6;
        if (x + 6 > 128) {
            break;
        }
        str++;
    }
}

static adc_oneshot_unit_handle_t init_potentiometer_adc(void)
{
    adc_oneshot_unit_handle_t adc_handle = NULL;
    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle, POT_ADC_CHANNEL, &channel_config));
    return adc_handle;
}

static void init_serial_bridge(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_param_config(SERIAL_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(
        SERIAL_UART,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(SERIAL_UART, 256, 256, 0, NULL, 0));
}

static bool read_serial_line(char *line, size_t line_size)
{
    static size_t line_length = 0;
    static char line_buffer[64];
    uint8_t byte;

    while (uart_read_bytes(SERIAL_UART, &byte, 1, 0) == 1) {
        if (byte == '\n' || byte == '\r') {
            if (line_length == 0) {
                continue;
            }
            size_t copy_length = line_length < line_size - 1
                ? line_length
                : line_size - 1;
            memcpy(line, line_buffer, copy_length);
            line[copy_length] = '\0';
            line_length = 0;
            return true;
        }

        if (line_length < sizeof(line_buffer) - 1) {
            line_buffer[line_length++] = (char)byte;
        }
    }

    return false;
}

static void render_multizone_ui(int percent, int raw_value, const char *message)
{
    char value_line[24];
    char raw_line[24];

    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    snprintf(value_line, sizeof(value_line), "VALUE: %d%%", percent);
    snprintf(raw_line, sizeof(raw_line), "RAW: %d", raw_value);

    oled_clear();
    oled_draw_string(0, 0, "ESP32 OK", true);
    oled_draw_string(0, 16, value_line, true);
    oled_draw_string(0, 32, raw_line, true);
    oled_draw_string(0, 48, message, true);
    oled_flush();
}

static int adc_to_percent(int raw_value)
{
    const int raw_min = 150;
    const int raw_max = 3950;

    if (raw_value <= raw_min) {
        return 0;
    }
    if (raw_value >= raw_max) {
        return 100;
    }
    return ((raw_value - raw_min) * 100) / (raw_max - raw_min);
}

static bool parse_serial_command(const char *line, int *percent, char *message)
{
    if (sscanf(line, "SET:%d:%31[^\r\n]", percent, message) == 2) {
        return true;
    }

    if (sscanf(line, "%d,%31[^\r\n]", percent, message) == 2) {
        return true;
    }

    message[0] = '\0';
    return false;
}

static void oled_dump_forensic_buffer(void)
{
    const int start_col = 30;
    const int dump_length = 16;

    ESP_LOGI("FORENSIC", "=== DUMPING FRAMEBUFFER PAGE 0 (Cols 30-45) ===");
    for (int offset = 0; offset < dump_length; offset++) {
        int col = start_col + offset;
        uint8_t byte = s_oled_buffer[col];
        printf("Byte[%2d] (Col %2d): 0x%02X  [Binary: %c%c%c%c%c%c%c%c]\n",
               col, col, byte,
               (byte & 0x80) ? '1' : '0',
               (byte & 0x40) ? '1' : '0',
               (byte & 0x20) ? '1' : '0',
               (byte & 0x10) ? '1' : '0',
               (byte & 0x08) ? '1' : '0',
                             (byte & 0x04) ? '1' : '0',
               (byte & 0x02) ? '1' : '0',
               (byte & 0x01) ? '1' : '0');
    }
}

static void oled_dump_font_pattern_forensic(char c)
{
    int idx = c - 32;
    printf("Forensic reconstruction for '%c' (ASCII %d)\n", c, c);
    for (int col = 0; col < 5; col++) {
        uint8_t byte = font5x7[idx][col];
        printf("Col %d: 0x%02X  [Binary: %c%c%c%c%c%c%c%c]\n",
               col, byte,
               (byte & 0x80) ? '1' : '0',
               (byte & 0x40) ? '1' : '0',
               (byte & 0x20) ? '1' : '0',
               (byte & 0x10) ? '1' : '0',
               (byte & 0x08) ? '1' : '0',
               (byte & 0x04) ? '1' : '0',
               (byte & 0x02) ? '1' : '0',
               (byte & 0x01) ? '1' : '0');
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(oled_spi_init());

    gpio_set_level(OLED_PIN_RES, 0);
    vTaskDelay(pdMS_TO_TICKS(15));
    gpio_set_level(OLED_PIN_RES, 1);
    vTaskDelay(pdMS_TO_TICKS(15));

    oled_send_cmd(0xAE); // Display OFF
    oled_send_cmd(0x8D); // Charge Pump Setting
    oled_send_cmd(0x14); // Enable Charge Pump
    oled_send_cmd(0x20); // Addressing Mode
    oled_send_cmd(0x00); // Horizontal Mode
    oled_send_cmd(0xAF); // Display ON

    uint8_t buffer[128];
    memset(buffer, 0xFF, sizeof(buffer));

    oled_send_cmd(0x21);
    oled_send_cmd(0x00);
    oled_send_cmd(0x7F);

    oled_send_cmd(0x22);
    oled_send_cmd(0x00);
    oled_send_cmd(0x07);

    for (int page = 0; page < 8; page++) {
        oled_send_data(buffer, sizeof(buffer));
    }

    vTaskDelay(pdMS_TO_TICKS(1500));

    oled_clear();
    oled_draw_pixel(0, 0, true);
    oled_draw_pixel(127, 0, true);
    oled_draw_pixel(0, 63, true);
    oled_draw_pixel(127, 63, true);
    oled_flush();

    vTaskDelay(pdMS_TO_TICKS(1500));

    oled_clear();
    oled_draw_string(30, 2, "Hello World", true);
    oled_draw_string(28, 24, "ID: 67030109", true);
    oled_draw_string(28, 46, "ID: 67030085", true);
    oled_flush();

    vTaskDelay(pdMS_TO_TICKS(1000));
    oled_dump_forensic_buffer();
    oled_dump_font_pattern_forensic('H');

    adc_oneshot_unit_handle_t adc_handle = init_potentiometer_adc();
    init_serial_bridge();
    char rx_buffer[64];
    char message[32] = "WAITING SERVER";
    int percent = 0;
    int raw_value = 0;

    render_multizone_ui(percent, raw_value, message);
    ESP_LOGI("BRIDGE", "Two-way serial bridge ready: ADC GPIO34 -> UART0");

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, POT_ADC_CHANNEL, &raw_value));
        percent = adc_to_percent(raw_value);
        printf("ADC:%d\n", raw_value);
        render_multizone_ui(percent, raw_value, message);

        if (read_serial_line(rx_buffer, sizeof(rx_buffer))) {
            char received_message[32];
            int received_percent;

            if (parse_serial_command(rx_buffer, &received_percent, received_message)) {
                strncpy(message, received_message, sizeof(message) - 1);
                message[sizeof(message) - 1] = '\0';
                ESP_LOGI("BRIDGE", "Received SET:%d:%s", received_percent, message);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}