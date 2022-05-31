#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "esp_modem_api.h"
#include "driver/gpio.h"

#define TAG "esp_modem_test"

static EventGroupHandle_t event_group = NULL;
static const int CONNECT_BIT = BIT0;

// Gestion du PPP
//
static void on_ppp_changed(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    ESP_LOGI(TAG, "PPP state changed event %d", event_id);
    if (event_id == NETIF_PPP_ERRORUSER)
    {
        esp_netif_t *netif = event_data;
        ESP_LOGI(TAG, "User interrupted event from netif:%p", netif);
    }
}

static void on_ip_event(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data)
{
    ESP_LOGD(TAG, "IP event! %d", event_id);
    if (event_id == IP_EVENT_PPP_GOT_IP)
    {
        esp_netif_dns_info_t dns_info;

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_netif_t *netif = event->esp_netif;

        ESP_LOGI(TAG, "Modem Connect to PPP Server");
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        ESP_LOGI(TAG, "IP          : " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Netmask     : " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Gateway     : " IPSTR, IP2STR(&event->ip_info.gw));
        esp_netif_get_dns_info(netif, 0, &dns_info);
        ESP_LOGI(TAG, "Name Server1: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        esp_netif_get_dns_info(netif, 1, &dns_info);
        ESP_LOGI(TAG, "Name Server2: " IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
        ESP_LOGI(TAG, "~~~~~~~~~~~~~~");
        xEventGroupSetBits(event_group, CONNECT_BIT);

        ESP_LOGI(TAG, "GOT ip event!!!");
    }
    else if (event_id == IP_EVENT_PPP_LOST_IP)
    {
        ESP_LOGI(TAG, "Modem Disconnect from PPP Server");
    }
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_VERBOSE);

    ESP_LOGI(TAG, "Startup..");
    ESP_LOGI(TAG, "Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

    // Init and register system/core components */
    //
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &on_ip_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, &on_ppp_changed, NULL));

    event_group = xEventGroupCreate();

    // Reset modem
    //
    ESP_LOGI(TAG, "Power-on / reset modem START");

#define MODEM_PWKEY 4
#define MODEM_RST 5
#define MODEM_POWER_ON 23
#define MODEM_DTR 25

    gpio_set_direction(MODEM_PWKEY, GPIO_MODE_OUTPUT);    // MODEM_PWKEY
    gpio_set_direction(MODEM_RST, GPIO_MODE_OUTPUT);      // MODEM_RST
    gpio_set_direction(MODEM_POWER_ON, GPIO_MODE_OUTPUT); // MODEM_POWER_ON
    gpio_set_direction(MODEM_DTR, GPIO_MODE_OUTPUT);      // MODEM_DTR

    gpio_set_level(MODEM_PWKEY, 1);
    gpio_set_level(MODEM_RST, 1);
    gpio_set_level(MODEM_POWER_ON, 0);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    gpio_set_level(MODEM_POWER_ON, 1);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    gpio_set_level(MODEM_PWKEY, 0);

    vTaskDelay(1000 / portTICK_PERIOD_MS);
    gpio_set_level(MODEM_PWKEY, 1);
    gpio_set_level(MODEM_RST, 1);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    gpio_set_level(MODEM_RST, 0);

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    gpio_set_level(MODEM_RST, 1);

    vTaskDelay(15000 / portTICK_PERIOD_MS);

    gpio_set_level(MODEM_DTR, 0);

    ESP_LOGI(TAG, "Power-on / reset modem END");

    // Configure the DTE
    //
    esp_modem_dte_config_t dte_config = ESP_MODEM_DTE_DEFAULT_CONFIG();
    dte_config.uart_config.tx_io_num = 27;
    dte_config.uart_config.rx_io_num = 26;
    dte_config.uart_config.rx_buffer_size = 1024;
    dte_config.uart_config.tx_buffer_size = 512;
    dte_config.uart_config.cts_io_num = UART_PIN_NO_CHANGE;
    dte_config.uart_config.rts_io_num = UART_PIN_NO_CHANGE;
    dte_config.uart_config.event_queue_size = 30;
    dte_config.task_stack_size = 2048;
    dte_config.task_priority = 5;
    dte_config.dte_buffer_size = 1024 / 2;

    dte_config.uart_config.baud_rate = 115200;
    dte_config.uart_config.data_bits = UART_DATA_8_BITS;
    dte_config.uart_config.parity = UART_PARITY_DISABLE;
    dte_config.uart_config.stop_bits = UART_STOP_BITS_1;
    dte_config.uart_config.flow_control = UART_HW_FLOWCTRL_DISABLE;

    // Configure the DCE
    //
    esp_modem_dce_config_t dce_config = ESP_MODEM_DCE_DEFAULT_CONFIG("iot.1nce.net");

    esp_netif_config_t netif_ppp_config = ESP_NETIF_DEFAULT_PPP();
    esp_netif_t *esp_netif = esp_netif_new(&netif_ppp_config);
    assert(esp_netif);
    ESP_LOGI(TAG, "Initializing esp_modem for the SIMCOM module...");
    // esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7600, &dte_config, &dce_config, esp_netif);
    //esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM7000, &dte_config, &dce_config, esp_netif);
    esp_modem_dce_t *dce = esp_modem_new_dev(ESP_MODEM_DCE_SIM800, &dte_config, &dce_config, esp_netif);
    assert(dce);

    esp_err_t err;

    err = esp_modem_set_mode(dce, ESP_MODEM_MODE_DATA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_modem_set_mode(ESP_MODEM_MODE_DATA) failed with %d", err);
        return;
    }

    xEventGroupWaitBits(event_group, CONNECT_BIT, pdTRUE, pdTRUE, portMAX_DELAY);
}
