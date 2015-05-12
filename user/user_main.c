/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#include "ets_sys.h"
#include "stdout/stdout.h"
//#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "httpd.h"
#include "httpclient.h"
#include "json/jsonparse.h"
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))
#define bitToggle(value, bit) ((value) ^= (1UL << (bit)))
#define HIGH 0x1
#define LOW  0x0

static ETSTimer RegisterTimer;

//Pin connected to latch pin (ST_CP) of 74HC595
const int latchPin = 0;
//Pin connected to clock pin (SH_CP) of 74HC595
const int clockPin = 2;
////Pin connected to Data in (DS) of 74HC595

const int dataPin = 3;

const char kHostname[] = "192.168.1.102";

#define digitalWrite GPIO_OUTPUT_SET


#define LSBFIRST 0
#define MSBFIRST 1

#define page "<html> <body> <form action='.' method='post'>  SSID: <input type='text' name='ssid'><br>  Password: <input type='text' name='password'><br>  <input type='submit' value='Submit'></form></body></html>"


MQTT_Client mqttClient;


int connection_attempts = 0;

int register_state = 0;

char * csrf;
char * session;




static ETSTimer rebootTimer;

os_event_t    user_procTaskQueue[user_procTaskQueueLen];


void ICACHE_FLASH_ATTR shiftOut(uint8_t dataPin, uint8_t clockPin, uint8_t bitOrder, uint8_t val)
{
    uint8_t i;

    for (i = 0; i < 8; i++)  {
        if (bitOrder == LSBFIRST)
            digitalWrite(dataPin, !!(val & (1 << i)));
        else
            digitalWrite(dataPin, !!(val & (1 << (7 - i))));

        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}



char ICACHE_FLASH_ATTR * detectCookie(char* buf, char* cookie_name)
{
    //Serial.println(buf);
    char* index = strstr(buf, cookie_name);
    if(index != NULL) {
        char* tmp = index;
        while(*tmp != ';') {
            tmp ++;
        }
        *tmp='\0';
        index += strlen(cookie_name) + 1;
        //Serial.println(index);
        return index;

    }
}



void ICACHE_FLASH_ATTR headers_read(char * headers)
{

    int i;
    for (i=0; i++; i< strlen(headers))
        if ( headers[i] == '\n') {
            if (csrf == NULL) {
                csrf = detectCookie(headers, "csrftoken");
            }
            if (session == NULL) {
                session = detectCookie(headers, "sessionid");
            }
            if (csrf != NULL && session != NULL) {
                break;
            }
        }
}



void ICACHE_FLASH_ATTR push_to_register()
{
    INFO("PUSH\n");
    digitalWrite(latchPin, LOW);
    shiftOut(dataPin, clockPin, MSBFIRST, register_state);
    digitalWrite(latchPin, HIGH);

}
char main_topic[50];



int ICACHE_FLASH_ATTR cgiWiFiScan(HttpdConnData *connData)
{
    char buff[2048];
    char ssid[32], passwd[64];
    int result = httpdFindArg(connData->postBuff, "ssid", ssid, sizeof(ssid));

    if (connData->postLen > 10) {
        result = httpdFindArg(connData->postBuff, "password", passwd, sizeof(passwd));
        if (result != -1) {
            os_strncpy((char*)sysCfg.sta_ssid, ssid, 32);
            os_strncpy((char*)sysCfg.sta_pwd, passwd, 64);
            os_strcpy(buff, sysCfg.sta_ssid);
            httpdSend(connData, buff, -1);
            CFG_Save();
            os_timer_setfn(&rebootTimer, (os_timer_func_t *)system_restart, NULL);
            os_timer_arm(&rebootTimer, 5000, 0);
            return HTTPD_CGI_DONE;

        }

    }

    httpdSend(connData, page , -1);

    return HTTPD_CGI_DONE;


}


void ICACHE_FLASH_ATTR my_http_callback(char * response, int http_status, char * full_response, char * headers)
{

    os_printf(headers);
    os_printf("http_status=%d\n", http_status);
    os_printf("strlen(response)=%d\n", strlen(response));
    os_printf("strlen(full_response)=%d\n", strlen(full_response));
    os_printf("response=%s\n(end)\n", response);
}


HttpdBuiltInUrl builtInUrls[]= {
    {"*", cgiWiFiScan, NULL},
    {NULL, NULL, NULL}
};

void ICACHE_FLASH_ATTR wifiFailedCB(uint8_t status)
{
    INFO("Wifi connect failed, starting fallback");
    httpdInit(builtInUrls, 80);
}

void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args)
{
    char queue_name[100];
    MQTT_Client* client = (MQTT_Client*)args;
    int id = system_get_chip_id();
    INFO("MQTT: Connected\r\n");
    os_sprintf(main_topic, "/smart_plug_work/SmartPlug-%d", id);
    MQTT_Subscribe(client, main_topic, 0);

}

void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Disconnected\r\n");
}

void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Published\r\n");
}

void ICACHE_FLASH_ATTR flip_switch(int swit)
{

    char buf[9];
    int i;
    swit--;
    bitToggle(register_state, swit);

    push_to_register();



    for (i=0; i<8; i++) {
        if (bitRead(register_state, i) == true) {
            buf[i] = '1';
        } else {
            buf[i] = '0';
        }


    }
    buf[8] = '\0';
    INFO(buf);
    MQTT_Publish(&mqttClient, "/results", buf, sizeof(buf), 0,
                 0);

}

void  ICACHE_FLASH_ATTR process_command(char * data)
{


    char cmd[50];

    if ( get_json_value(data, "command", cmd, 50) == 1) {


        if (strcmp(cmd, "switch" ) == 0) {

            int switch_num;
            if (get_json_value(data, "switchnum", &switch_num, 1) == 1) {
                os_printf("%d", switch_num);
                flip_switch(switch_num);
            }


        }
        INFO(cmd);

    }

}




void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
    char *topicBuf = (char*)os_zalloc(topic_len+1),
          *dataBuf = (char*)os_zalloc(data_len+1);

    MQTT_Client* client = (MQTT_Client*)args;

    os_memcpy(topicBuf, topic, topic_len);
    topicBuf[topic_len] = 0;

    os_memcpy(dataBuf, data, data_len);
    dataBuf[data_len] = 0;

    if( strcmp(topic, main_topic)) {
        process_command(dataBuf);
    }

    INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
    os_free(topicBuf);
    os_free(dataBuf);
}




void ICACHE_FLASH_ATTR mqtt_init()
{


    CFG_Load();

    MQTT_InitConnection(&mqttClient, "dmarkey.com", 8000, 0);


    MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, 30, 1);

    MQTT_OnConnected(&mqttClient, mqttConnectedCb);
    MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
    MQTT_OnPublished(&mqttClient, mqttPublishedCb);
    MQTT_OnData(&mqttClient, mqttDataCb);
    MQTT_Connect(&mqttClient);


}

void ICACHE_FLASH_ATTR wifiConnectCb(uint8_t status)
{
    if(status == STATION_GOT_IP) {
        char post_data[1000];

        int id = system_get_chip_id();
        os_sprintf(post_data, "type=1&controller_id=%d\n", id);
        INFO(post_data);

        http_post("http://dmarkey.com:8080/controller_ping_create/", post_data, NULL);
        mqtt_init();

    } else {
        MQTT_Disconnect(&mqttClient);
    }
}

int ICACHE_FLASH_ATTR get_json_value(char * input, char * name, char * output, int len )
{

    struct jsonparse_state parser;
    jsonparse_setup(&parser, input, strlen(input));
    int type, type1;

    while ((type = jsonparse_next(&parser)) != 0) {
        if (type == JSON_TYPE_PAIR_NAME) {
            if (jsonparse_strcmp_value(&parser, name) == 0) {
                jsonparse_next(&parser);
                type1 = jsonparse_next(&parser);
                if (type1 == JSON_TYPE_NUMBER) {
                    int output_int = jsonparse_get_value_as_int(&parser);
                    os_memcpy(output, &output_int, sizeof(output_int));
                    return 1;
                } else if (type1 == JSON_TYPE_STRING) {
                    jsonparse_copy_value(&parser, output, len);
                    INFO(output);
                    return 1;

                }

            }

        }
    }

    return 0;

}

static void ICACHE_FLASH_ATTR loop(os_event_t *events)
{


    CFG_Load();
    WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb, wifiFailedCB);

    INFO("\r\nSystem started ...\r\n");
    uart0_sendStr("\nREADY\n");


}




void user_init(void)
{
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_GPIO3);
    push_to_register();
    stdout_init();


    //os_delay_us(1000000);
    system_timer_reinit();
    INFO("Starting...");
    system_os_task(loop, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
    system_os_post(user_procTaskPrio, 0, 0 );

}
