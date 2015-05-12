/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */
#include "wifi.h"
#include "user_interface.h"
#include "osapi.h"
#include "espconn.h"
#include "os_type.h"
#include "mem.h"
#include "mqtt_msg.h"
#include "debug.h"
#include "user_config.h"
#include "config.h"

static ETSTimer WiFiLinker;
WifiCallback wifiCb = NULL;
WifiCallback wifiFailConnectCb = NULL;
static uint8_t wifiStatus = STATION_IDLE, lastWifiStatus = STATION_IDLE;

int retries = 0;
int in_fallback_mode = 0;



void fallback_mode(){
    
    retries++;
    
    if (retries == 5)
    {   
        in_fallback_mode = 1;
        
        wifi_set_opmode(SOFTAP_MODE);
        //wifi_set_opmode(NULL_MODE);
        //wifi_set_phy_mode(PHY_MODE_11G);
        struct softap_config SAP_Config;
        int id = system_get_chip_id();
        os_sprintf(SAP_Config.ssid, "SmartPlug-%d", id);
        os_memcpy(&SAP_Config.password, "smartplug", 32);
        SAP_Config.ssid_hidden = 0;
        SAP_Config.channel = 1;
        SAP_Config.authmode = AUTH_WPA2_PSK;
        SAP_Config.ssid_len = 0;
        SAP_Config.max_connection=50;
        wifi_softap_set_config(&SAP_Config);
        
        //wifi_set_opmode(SOFTAP_MODE);
        
        //wifi_softap_dhcps_stop()
        //wifi_softap_dhcps_start()
        //wifi_station_set_auto_connect(FALSE);
        wifi_station_disconnect();
        wifiFailConnectCb(0);
        in_fallback_mode = 1;
        os_timer_disarm(&WiFiLinker);
        
        
    }
    else{
                wifi_station_connect();
    }
        
        
    //wifi_set_opmode(STATIONAP_MODE);
    
}



static void ICACHE_FLASH_ATTR wifi_check_ip(void *arg)
{
        if (in_fallback_mode == 1){
        
            
            return;
            
        }

	struct ip_info ipConfig;

	os_timer_disarm(&WiFiLinker);
	wifi_get_ip_info(STATION_IF, &ipConfig);
	wifiStatus = wifi_station_get_connect_status();
        //INFO(ipConfig.ip.addr);
	if (wifiStatus == STATION_GOT_IP && ipConfig.ip.addr != 0)
	{

		os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
		os_timer_arm(&WiFiLinker, 5000, 0);


	}
	else
	{
            os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
            os_timer_arm(&WiFiLinker, 5000, 0);
            fallback_mode();
		/*if(wifi_station_get_connect_status() == STATION_WRONG_PASSWORD)
		{
                        fallback_mode();

			INFO("STATION_WRONG_PASSWORD\r\n");
			wifi_station_connect();


		}
		else if(wifi_station_get_connect_status() == STATION_NO_AP_FOUND)
		{
                        fallback_mode();

			INFO("STATION_NO_AP_FOUND\r\n");
			wifi_station_connect();


		}
		else if(wifi_station_get_connect_status() == STATION_CONNECT_FAIL)
		{
                        fallback_mode();

			INFO("STATION_CONNECT_FAIL\r\n");
			wifi_station_connect();

		}
		else
		{
			INFO("STATION_IDLE\r\n");
                        fallback_mode();

                        wifi_station_connect();
		}*/
                
                



	}
	if(wifiStatus != lastWifiStatus){
		lastWifiStatus = wifiStatus;
		if(wifiCb)
			wifiCb(wifiStatus);
	}
}


/*void shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val)
{
    uint8_t i;
    
    for (i = 0; i < 8; i++)  {

        digitalWrite(dataPin, !!(val & (1 << (7 - i))));
        
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}*/


void ICACHE_FLASH_ATTR WIFI_Connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb, WifiCallback failed)
{
    
	struct station_config stationConf;
        wifi_set_opmode(STATION_MODE);
        struct softap_config SAP_Config;
        os_memcpy(&SAP_Config.ssid, "Hidden", 32);
        os_memcpy(&SAP_Config.password, "random", 32);
        SAP_Config.ssid_hidden = 1;
        SAP_Config.ssid_len = 0;
        SAP_Config.channel = 1;
        SAP_Config.authmode = AUTH_WPA2_PSK;
        
        wifi_softap_set_config(&SAP_Config);
        
        
        
	INFO("WIFI_INIT\r\n");
        //wifi_station_set_auto_connect(0);
	
	
	wifiCb = cb;
        wifiFailConnectCb = failed;

	os_memset(&stationConf, 0, sizeof(struct station_config));

	os_sprintf(stationConf.ssid, "%s", ssid);
	os_sprintf(stationConf.password, "%s", pass);
    INFO(ssid);

	wifi_station_set_config(&stationConf);

	os_timer_disarm(&WiFiLinker);
	os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
	os_timer_arm(&WiFiLinker, 1000, 0);
        
        wifi_station_set_auto_connect(0);
	wifi_station_connect();
}

