/**
 *
 * The code GET traffic data from TomTom API and publish it to AWS IoT core in every 60 seconds
 * on MQTT protocol.
 * The example is single threaded and uses statically allocated memory. It uses QOS0 for Publish messages.
 */
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "aws_iot_mqtt_client.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "esp_tls.h"

extern const char *TAG;
static const uint8_t aws_root_ca_pem_start[] asm("_binary_aws_root_ca_pem_start");
static const uint8_t aws_root_ca_pem_end[] asm("_binary_aws_root_ca_pem_end");
static const uint8_t certificate_pem_crt_start[] asm("_binary_certificate_pem_crt_start");
static const uint8_t certificate_pem_crt_end[] asm("_binary_certificate_pem_crt_end");
static const uint8_t private_pem_key_start[] asm("_binary_private_pem_key_start");
static const uint8_t private_pem_key_end[] asm("_binary_private_pem_key_end");
static const char *PUBTOPIC = "esp32/traffic/data";
char payload[200];


#define MAX_HTTP_OUTPUT_BUFFER 2048

esp_err_t _http_event_handle(esp_http_client_event_t *evt)
{   
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;  // Stores number of bytes read   
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, %d",evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                    // If user_data buffer is configured, copy the response into the buffer
               
                if (evt->user_data) 
                {
                    memcpy(evt->user_data + output_len, evt->data, evt->data_len);
                } 
                else 
                {
                    if (output_buffer == NULL) 
                    {
                        output_buffer = (char *) malloc(esp_http_client_get_content_length(evt->client));
                        output_len = 0;
                        if (output_buffer == NULL)
                        {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                        memcpy(output_buffer + output_len, evt->data, evt->data_len);
                }
                output_len += evt->data_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) 
            {   
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error(evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                if (output_buffer != NULL) {
                    free(output_buffer);
                    output_buffer = NULL;
                }
                output_len = 0;
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            break;
    }
    return ESP_OK;

}

void disconnectCallbackHandler(AWS_IoT_Client *pClient, void *data) 
{
    ESP_LOGW(TAG, "MQTT Disconnected");
}


void iot_subscribe_callback_handler(AWS_IoT_Client *pClient, char *topicName, uint16_t topicNameLen,
                                    IoT_Publish_Message_Params *params, void *pData) 
{
    ESP_LOGI(TAG, "Receiving data from AWS");
    ESP_LOGI(TAG, "%.*s\t%.*s", topicNameLen, topicName, (int) params->payloadLen, (char *)params->payload);
}

//Refer to Sample API section at https://github.com/espressif/aws-iot-device-sdk-embedded-C/tree/61f25f34712b1513bf1cb94771620e9b2b001970
void aws_connect_task(void *param)
{ 

   IoT_Error_t rc = FAILURE;
   //int32_t i = 0;

   AWS_IoT_Client client;
   IoT_Client_Init_Params mqttInitParams = iotClientInitParamsDefault;
   IoT_Client_Connect_Params connectParams = iotClientConnectParamsDefault;
    
   mqttInitParams.enableAutoReconnect = false; // We enable this later below
   mqttInitParams.pHostURL = AWS_IOT_MQTT_HOST;
   mqttInitParams.port = AWS_IOT_MQTT_PORT;
   
   //Refer to https://docs.espressif.com/projects/esp-jumpstart/en/latest/remotecontrol.html#embedding-files-in-the-firmware
   mqttInitParams.pRootCALocation = (const char *)aws_root_ca_pem_start;
   mqttInitParams.pDeviceCertLocation = (const char *)certificate_pem_crt_start;
   mqttInitParams.pDevicePrivateKeyLocation = (const char *)private_pem_key_start;

    mqttInitParams.mqttCommandTimeout_ms = 20000;
    mqttInitParams.tlsHandshakeTimeout_ms = 5000;
    mqttInitParams.isSSLHostnameVerify = true;
    mqttInitParams.disconnectHandler = disconnectCallbackHandler;
    mqttInitParams.disconnectHandlerData = NULL;

    connectParams.keepAliveIntervalInSec = 61; //should be more than the vTaskDelay in while loop
    connectParams.isCleanSession = true;
    connectParams.MQTTVersion = MQTT_3_1_1;
    
    connectParams.pClientID = AWS_IOT_MQTT_CLIENT_ID;
    connectParams.clientIDLen = (uint16_t) strlen(AWS_IOT_MQTT_CLIENT_ID);
    connectParams.isWillMsgPresent = false;
   /*Initialize a new MQTT client context. This function should be called before any 
    other MQTT function to initialize a new MQTT client context. Once the client 
    is no longer needed, @ref mqtt_function_free should be called. */
    ESP_LOGI(TAG, "Init MQTT");
    rc = aws_iot_mqtt_init(&client, &mqttInitParams);
    if(SUCCESS != rc) 
    {
        ESP_LOGE(TAG, "aws_iot_mqtt_init returned error : %d ", rc);
        //vTaskDelete(NULL);
        abort();
    }

    ESP_LOGI(TAG, "Connecting to AWS...");
    rc = aws_iot_mqtt_connect(&client, &connectParams);
    do
      {
        if(SUCCESS != rc) 
        {
            ESP_LOGE(TAG, "Error(%d) connecting to %s:%d", rc, mqttInitParams.pHostURL, mqttInitParams.port);
            //vTaskDelay(1000 / portTICK_RATE_MS);
        }
      }while(SUCCESS != rc);  


    /*
     * Enable Auto Reconnect functionality. Minimum and Maximum time of Exponential backoff are set in aws_iot_config.h
     *  #AWS_IOT_MQTT_MIN_RECONNECT_WAIT_INTERVAL
     *  #AWS_IOT_MQTT_MAX_RECONNECT_WAIT_INTERVAL
     */
    rc = aws_iot_mqtt_autoreconnect_set_status(&client, true);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Unable to set Auto Reconnect to true - %d", rc);
        abort();
    }
    
    const int topic_len = strlen(PUBTOPIC);

    IoT_Publish_Message_Params paramsQOS0;
    paramsQOS0.qos = QOS0;
    paramsQOS0.isRetained = 0;
    

    //*****************************************************************************************
    //Getting Data from API
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = "{\"abcdef\" : 4}";
    esp_http_client_config_t config = 
    {
        .url = "https://api.tomtom.com/traffic/services/4/flowSegmentData/absolute/10/json?point=48.791672%2C2.344767&unit=KMPH&key=XXXXXXXXXXXXXXXX",
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handle,
        .user_data = local_response_buffer
    };
    esp_http_client_handle_t httpClient = esp_http_client_init(&config);
    //task loop
    while((NETWORK_ATTEMPTING_RECONNECT == rc || NETWORK_RECONNECTED == rc || SUCCESS == rc)) 
    {
        
        if(NETWORK_ATTEMPTING_RECONNECT == rc)
        {
            // If the client is attempting to reconnect we will skip the rest of the loop.
            continue;
        }
        esp_err_t err = esp_http_client_perform(httpClient);
        if (err == ESP_OK)
        {        
                ESP_LOGI(TAG, "Status = %d, content_length = %d",
                esp_http_client_get_status_code(httpClient),
                esp_http_client_get_content_length(httpClient));
                paramsQOS0.payload = local_response_buffer;
                paramsQOS0.payloadLen = strlen(local_response_buffer);
                ESP_LOGI(TAG, " Sending JSON Response to AWS : %s", local_response_buffer);
                rc = aws_iot_mqtt_publish(&client, PUBTOPIC, topic_len, &paramsQOS0);
        }
       
        ESP_LOGI(TAG, "Stack remaining for task '%s' is %d bytes", pcTaskGetTaskName(NULL), uxTaskGetStackHighWaterMark(NULL));
        vTaskDelay(60000 / portTICK_RATE_MS); // 60 second delay
    } //task loop ends
    
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "An error occurred in the loop %d", rc);
    }

    ESP_LOGI(TAG, "Disconnecting with AWS");
    rc = aws_iot_mqtt_disconnect(&client);
    esp_http_client_cleanup(httpClient);
    if(SUCCESS != rc) {
        ESP_LOGE(TAG, "Disconnect error %d", rc);
    }
    
    vTaskDelete(NULL); //Neccessary in each task function      
}

//Refer to subscribe_publish_sample.c
void aws_connect_init(void)
{   //9216
    ESP_LOGI(TAG, "Starting cloud\n");
    if (xTaskCreate(&aws_connect_task, "aws_iot_task", 11000, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Couldn't create cloud task\n");
        /* Indicate error to user */
    }
    //return ESP_OK;
}