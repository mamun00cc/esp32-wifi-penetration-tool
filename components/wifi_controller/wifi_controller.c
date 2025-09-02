#include "wifi_controller.h"

#include <stdio.h>
#include <string.h>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char* TAG = "wifi_controller";
/**
 * @brief Stores current state of Wi-Fi interface
 */
static bool wifi_init = false;
static uint8_t original_mac_ap[6];

static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data){
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_START:
                ESP_LOGI(TAG, "AP started");
                break;
            case WIFI_EVENT_AP_STOP:
                ESP_LOGI(TAG, "AP stopped");
                break;
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "STA started");
                break;
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "STA connected to AP");
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "STA disconnected from AP");
                esp_wifi_connect(); // auto-reconnect
                break;
            default:
                break;
        }
    }
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "STA got IP");
    }
}

/**
 * @brief Initializes Wi-Fi interface into APSTA mode and starts it.
 * 
 * @attention This function should be called only once.
 */
static void wifi_init_apsta(){
    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    // save original AP MAC address
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, original_mac_ap));

    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_init = true;
}

void wifictl_ap_start(wifi_config_t *wifi_config) {
    ESP_LOGD(TAG, "Starting AP...");
    if(!wifi_init){
        wifi_init_apsta();
    }

    esp_err_t err = esp_wifi_set_config(ESP_IF_WIFI_AP, wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set AP config: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "AP started with SSID=%s", wifi_config->ap.ssid);
}

void wifictl_ap_stop(){
    ESP_LOGD(TAG, "Stopping AP...");
    wifi_config_t wifi_config = {
        .ap = {
            .max_connection = 0
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
    ESP_LOGD(TAG, "AP stopped");
}

void wifictl_mgmt_ap_start(){
    wifi_config_t mgmt_wifi_config = {0};
    strncpy((char *)mgmt_wifi_config.ap.ssid, CONFIG_MGMT_AP_SSID, sizeof(mgmt_wifi_config.ap.ssid));
    mgmt_wifi_config.ap.ssid_len = strlen(CONFIG_MGMT_AP_SSID);
    strncpy((char *)mgmt_wifi_config.ap.password, CONFIG_MGMT_AP_PASSWORD, sizeof(mgmt_wifi_config.ap.password));
    mgmt_wifi_config.ap.max_connection = CONFIG_MGMT_AP_MAX_CONNECTIONS;
    mgmt_wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    mgmt_wifi_config.ap.channel = 1;
    mgmt_wifi_config.ap.ssid_hidden = 0;
    mgmt_wifi_config.ap.pmf_cfg.capable = false;
    mgmt_wifi_config.ap.pmf_cfg.required = false;
    wifictl_ap_start(&mgmt_wifi_config);
}

void wifictl_sta_connect_to_ap(const wifi_ap_record_t *ap_record, const char password[]){
    ESP_LOGD(TAG, "Connecting STA to AP...");
    if(!wifi_init){
        wifi_init_apsta();
    }

    wifi_config_t sta_wifi_config = {0};
    sta_wifi_config.sta.channel = ap_record->primary;
    sta_wifi_config.sta.scan_method = WIFI_FAST_SCAN;
    sta_wifi_config.sta.pmf_cfg.capable = false;
    sta_wifi_config.sta.pmf_cfg.required = false;
    memcpy(sta_wifi_config.sta.ssid, ap_record->ssid, sizeof(sta_wifi_config.sta.ssid));

    if(password != NULL){
        if(strlen(password) > 63) {
            ESP_LOGE(TAG, "Password is too long. Max supported length is 63");
            return;
        }
        strncpy((char *)sta_wifi_config.sta.password, password, sizeof(sta_wifi_config.sta.password));
    }

    ESP_LOGD(TAG, ".ssid=%s", sta_wifi_config.sta.ssid);

    esp_err_t err = esp_wifi_set_config(ESP_IF_WIFI_STA, &sta_wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set STA config: %s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(esp_wifi_connect());

}

void wifictl_sta_disconnect(){
    ESP_ERROR_CHECK(esp_wifi_disconnect());
}

void wifictl_set_ap_mac(const uint8_t *mac_ap){
    ESP_LOGD(TAG, "Changing AP MAC address...");
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_AP, mac_ap));
}

void wifictl_get_ap_mac(uint8_t *mac_ap){
    esp_wifi_get_mac(WIFI_IF_AP, mac_ap);
}

void wifictl_restore_ap_mac(){
    ESP_LOGD(TAG, "Restoring original AP MAC address...");
    ESP_ERROR_CHECK(esp_wifi_set_mac(WIFI_IF_AP, original_mac_ap));
}

void wifictl_get_sta_mac(uint8_t *mac_sta){
    esp_wifi_get_mac(WIFI_IF_STA, mac_sta);
}

void wifictl_set_channel(uint8_t channel){
    if((channel == 0) || (channel >  13)){
        ESP_LOGE(TAG,"Channel out of range. Expected value from <1,13> but got %u", channel);
        return;
    }
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}