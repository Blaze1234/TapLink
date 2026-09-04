#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_eth.h"
#include "esp_eth_phy_w5500.h"
#include "esp_eth_mac_w5500.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "TAP_CAPTURE";

// Shared SPI bus
#define PIN_MISO   21
#define PIN_MOSI   23
#define PIN_SCLK   5

// Module A
#define PIN_CS_A   20
#define PIN_INT_A  4
#define PIN_RST_A  22

// Module B
#define PIN_CS_B   32
#define PIN_INT_B  33
#define PIN_RST_B  36

static esp_eth_handle_t eth_handle_a = NULL;
static esp_eth_handle_t eth_handle_b = NULL;

// Called for every raw Ethernet frame received (bypasses the IP stack entirely)
static esp_err_t pkt_capture_cb(esp_eth_handle_t eth_handle, uint8_t *buffer, uint32_t length, void *priv)
{
    const char *label = (eth_handle == eth_handle_a) ? "A" : (eth_handle == eth_handle_b) ? "B" : "?";

    if (length >= 14) {
        ESP_LOGI(TAG, "[%s] Frame: %lu bytes | dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X type=0x%02X%02X",
                 label, (unsigned long)length,
                 buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5],
                 buffer[6], buffer[7], buffer[8], buffer[9], buffer[10], buffer[11],
                 buffer[12], buffer[13]);
    } else {
        ESP_LOGI(TAG, "[%s] Frame: %lu bytes (too short for full header)", label, (unsigned long)length);
    }
    free(buffer);  // required by the esp_eth raw input path contract
    return ESP_OK;
}

// Brings up one W5500 module as an independent Ethernet interface on the shared SPI bus.
// Auto-negotiation left at default (no forced link) -- this build is for
// plugging directly into a laptop, not for the tap's receive-only monitor ports.
static esp_eth_handle_t create_w5500_iface(int cs_pin, int int_pin, int rst_pin)
{
    spi_device_interface_config_t spi_devcfg = {
        .command_bits = 16,
        .address_bits = 8,
        .mode = 0,
        .clock_speed_hz = 20 * 1000 * 1000,
        .spics_io_num = cs_pin,
        .queue_size = 20,
    };

    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(SPI2_HOST, &spi_devcfg);
    w5500_config.base.int_gpio_num = int_pin;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = 1;
    phy_config.reset_gpio_num = rst_pin;

    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    ESP_ERROR_CHECK(esp_eth_update_input_path(eth_handle, pkt_capture_cb, NULL));

    bool promiscuous = true;
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_PROMISCUOUS, &promiscuous));

    return eth_handle;
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    spi_bus_config_t buscfg = {
        .miso_io_num = PIN_MISO,
        .mosi_io_num = PIN_MOSI,
        .sclk_io_num = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    eth_handle_a = create_w5500_iface(PIN_CS_A, PIN_INT_A, PIN_RST_A);
    ESP_ERROR_CHECK(esp_eth_start(eth_handle_a));
    ESP_LOGI(TAG, "Module A capture started");

    eth_handle_b = create_w5500_iface(PIN_CS_B, PIN_INT_B, PIN_RST_B);
    ESP_ERROR_CHECK(esp_eth_start(eth_handle_b));
    ESP_LOGI(TAG, "Module B capture started");
}