/* Hello World Example

 This example code is in the Public Domain (or CC0 licensed, at your option.)

 Unless required by applicable law or agreed to in writing, this
 software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 CONDITIONS OF ANY KIND, either express or implied.
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_spi_flash.h"
#include <esp_wifi.h>
#include <esp_event.h>
//#include "freertos/queue.h"
#include <esp_event_loop.h>

//#include <esp_log.h>
#include <nvs_flash.h>

#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

#include "string.h"
#include "EibnetIP.h"
#include "sdkconfig.h"
#include "globals.h"


static EventGroupHandle_t wifi_event_group;
const int CONNECTED_BIT = BIT0;

nvs_handle flash_handle;


extern void initTunnelingWithDiscovery();



ip4_addr_t ip;

wifi_config_t wifi_config = {
		.sta = {
				.ssid = "",
				.password =	""
		}
};

wifi_config_t apConfig = {
	   .ap = {
	      .ssid="ESP32_AP",
	      .ssid_len=0,
	      .password="esp32twoKnx",
	      .channel=0,
	      .authmode=WIFI_AUTH_WPA2_PSK,
	      .ssid_hidden=0,
	      .max_connection=1,
	      .beacon_interval=100
	   }
};


esp_err_t esp32_wifi_eventHandler(void *ctx, system_event_t *event) {
	esp_err_t err;
	switch (event->event_id) {
	case SYSTEM_EVENT_AP_START:
		printf("softAP started.\n");

		fflush(stdout);
		break;
	case SYSTEM_EVENT_AP_STOP:
		printf("softAP stopped.\n");
		fflush(stdout);
		break;
	case SYSTEM_EVENT_AP_STACONNECTED:
		printf("station connected.\n");
		fflush(stdout);
		break;
	case SYSTEM_EVENT_AP_STADISCONNECTED:
		printf("station disconnected.\n");
		fflush(stdout);
		break;
	case SYSTEM_EVENT_STA_START:{
		err = esp_wifi_connect();
		if (err == ESP_ERR_WIFI_SSID) {
			printf("****** Invalid AP SSID! %d ******\n", ESP_ERR_WIFI_SSID);
			ESP_ERROR_CHECK( esp_wifi_stop() );
			ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
			ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &apConfig) );
			ESP_ERROR_CHECK( esp_wifi_start() );
		}
		break;}
	case SYSTEM_EVENT_STA_GOT_IP:
		xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
		ip = event->event_info.got_ip.ip_info.ip;
		printf("Got IP: %s\n", inet_ntoa(ip));

		initTunnelingWithDiscovery();
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED: {
		xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
		system_event_sta_disconnected_t *disconnected = &event->event_info.disconnected;
		//printf("Connect failure (%d)!\n",disconnected->reason);
		if (disconnected->reason == WIFI_REASON_AUTH_FAIL || disconnected->reason == WIFI_REASON_AUTH_EXPIRE)
			printf("Failed authentification ( %d )\n", disconnected->reason);
		else if (disconnected->reason == WIFI_REASON_NO_AP_FOUND)
			printf("SSID not found ( %d )\n", disconnected->reason);
		else
			printf("Connect failure (%d)!\n", disconnected->reason);
		ESP_ERROR_CHECK( esp_wifi_stop() );
		if(disconnected->reason != WIFI_REASON_AUTH_EXPIRE){
			//start_dhcp_server();
			ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_APSTA) );
			ESP_ERROR_CHECK( esp_wifi_set_config(WIFI_IF_AP, &apConfig) );
			ESP_ERROR_CHECK( esp_wifi_start() );
		}

		/* This is a workaround as ESP32 WiFi libs don't currently
		 }
		 auto-reassociate. */
		//esp_wifi_connect();

		break;
	}
	default:
		break;
	}
	return ESP_OK;
}

void scanTask(){
	wifi_mode_t mode;
	while(1){
		vTaskDelay( 5000 / portTICK_PERIOD_MS );
		ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));
		if (mode==WIFI_MODE_APSTA || mode==WIFI_MODE_STA){
			wifi_scan_config_t scan_config = {
					.ssid = NULL,
					.bssid = NULL,
					.channel = 0,
					.show_hidden = true
			};
			esp_wifi_scan_start(&scan_config, false);
		}
	}
}

void initWifi() {
	tcpip_adapter_init();
	wifi_event_group = xEventGroupCreate();
	ESP_ERROR_CHECK(esp_event_loop_init(esp32_wifi_eventHandler, NULL));
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	printf("Setting WiFi configuration SSID %s...\n", wifi_config.sta.ssid);
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	xTaskCreate(&scanTask, "scanTask", 2048, NULL, 0, NULL);
}

void readWifiConfFromFlash(){
	size_t required_size;
	char SSID[32] = "";
	char pass[64] = "";
	esp_err_t err = nvs_flash_init();
	err = nvs_open("storage", NVS_READWRITE, &flash_handle);
	if (err != ESP_OK) {
		printf("Error (%d) opening NVS handle!\n", err);
		return;
	}
	err=nvs_get_str(flash_handle, "SSID", NULL, &required_size);
	if (err == ESP_ERR_NVS_NOT_FOUND) {
		printf("The value is not initialized yet! Set to 'Unknown' \n");
		err = nvs_set_str(flash_handle, "SSID", "Unknown");
	}
	else if(required_size>0){
		memset(SSID,0,32);
		nvs_get_str(flash_handle, "SSID", SSID, &required_size);
		memcpy(wifi_config.sta.ssid,SSID,32);
		nvs_get_str(flash_handle, "Pass", NULL, &required_size);
		if(required_size>0){
			memset(pass,0,64);
			nvs_get_str(flash_handle, "Pass", pass, &required_size);
			memcpy(wifi_config.sta.password,pass,64);
		}else{
			printf("No default config for WiFi!\n");
			memcpy(wifi_config.sta.ssid,"Unknown",32);
			memcpy(wifi_config.sta.password,"Unknown***",64);
		}

	}else {
		printf("No default config for WiFi!\n");
		memcpy(wifi_config.sta.ssid,"Unknown",32);
		memcpy(wifi_config.sta.password,"Unknown***",64);
	}

	printf("Saved SSID is = %s\n", SSID);
	nvs_commit(flash_handle);
	nvs_close(flash_handle);
}



void app_main() {
	/* Print chip information */
	size_t required_size;
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ", chip_info.cores,
			(chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
			(chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

	printf("silicon revision %d, ", chip_info.revision);
	printf("%dMB %s flash\n", spi_flash_get_chip_size() / (1024 * 1024),
			(chip_info.features & CHIP_FEATURE_EMB_FLASH) ?
					"embedded" : "external");

	readWifiConfFromFlash();
	initWifi();




	while (1) {
		vTaskDelay( 5000 / portTICK_PERIOD_MS );
	}
}
