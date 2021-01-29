#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"
#include "button_driver.h"
#include "mdns.h"
#include "aws_connect.h"
#include "freertos/event_groups.h"

/* Signal Wi-Fi events on this event-group */
const int WIFI_CONNECTED_BIT = BIT0;
static EventGroupHandle_t wifi_event_group;

const char *TAG = "AWS Connect";
static const char *service_name = "my_device";
static const char *service_key  = "password";
static const char *pop = "abcd1234";

//Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/provisioning/wifi_provisioning.html#user-side-implementation
void start_mdns_service(void)
{
    //initialize mDNS service
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    //set hostname
    mdns_hostname_set("my-esp32");
    //set default instance
    mdns_instance_name_set("Jhon's ESP32 Thing");
}


void nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_LOGI(TAG, "Erasing flash."); 
      ESP_ERROR_CHECK(nvs_flash_erase());
      ESP_ERROR_CHECK(nvs_flash_init());
    }
}


//Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_event.html#using-esp-event-apis
static void event_handler(void* handler_arg, esp_event_base_t event_base,
                          int event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT)
    {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
                case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT); //set bit number 0 to 1
                break;
            default:
                break;
        }
    }
    
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) // Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wifi-event-sta-start
    {   
        ESP_LOGI(TAG, "Connecting....");
        esp_wifi_connect();
    }
    else if(event_base == WIFI_EVENT && event_id == SYSTEM_EVENT_STA_DISCONNECTED) //Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wifi-event-sta-disconnected
    {   
        ESP_LOGI(TAG, "Disconnnected. retry to connect to the AP");
        esp_wifi_connect();
    }
    else if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) //Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#ip-event-sta-got-ip
    {   
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT); //set bit number 0 to 1
    } 
}


//Reference to this function at
//https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-lwip-init-phase
void init_wifi(void)
{   // create an LwIP core task and initialize the TCP/IP stack using esp_netif_init() call
    ESP_ERROR_CHECK(esp_netif_init());

    /* Create an event task/loop which is a daemon task(a task which keeps running in background in loop) 
    using esp_event_loop_create() which receives events from WiFi driver or from other subsystem, 
    such as TCPIP stack. This event task delivers events to the default callback function.
    Wi-Fi events are also handled by esp_netif component to provide a set of default behaviors.
    For example, when Wi-Fi station connects to an AP, esp_netif will automatically start the DHCP 
    client (by default).
    A handle of type esp_event_loop_handle_t is obtained (not returned) from the esp_event_loop_create().
    which can be used by the other APIs to reference the loop(event task) to perform their operations on.
    
    There is, however, a special type of event loop called the default event loop which is used here.
    The default event loop is a special type of loop used for system events (WiFi events, for example). 
    The handle for this loop is hidden from the user. */
    //ESP_ERROR_CHECK(esp_event_loop_create(constesp_event_loop_args_t *event_loop_args, esp_event_loop_handle_t *event_loop));
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    /* Attempt to create the event group. It is a group of bits(usually 8 bits) which can be set
    using xEventGroupSetBits().
    Refer to https://www.freertos.org/xEventGroupCreate.html#:~:text=Creates%20a%20new%20RTOS%20event,set%20to%201%20in%20FreeRTOSConfig. */
    wifi_event_group = xEventGroupCreate();

    /* Creates default WIFI AP and STA interface, Returns pointer to Network interface instance which binds
    station and ap with TCP/IP stack.
    Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_netif.html#wifi-default-initialization*/
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();
    esp_netif_set_hostname(sta_netif, "T-Rex");
    /*Create the Wi-Fi driver task and initialize the Wi-Fi driver. */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_LOGI(TAG, "init_wifi finished.");
}

//Refer to https://docs.espressif.com/projects/esp-idf/en/release-v4.0/api-reference/provisioning/wifi_provisioning.html#initialization
void init_wifi_provisioning(void)
{
    wifi_prov_mgr_config_t config = 
    {
    .scheme = wifi_prov_scheme_softap,
    .scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE
    };

    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));
}

//Reference to this function at
//https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-configuration-phase
void configure_wifi(void)
{   
    //configure the Wi-Fi mode as Station(it was initialised as AP due to provisioning)
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    //To  configure the specified interface ESP_IF_WIFI_STA
    ESP_ERROR_CHECK(esp_wifi_set_protocol(ESP_IF_WIFI_STA, WIFI_PROTOCOL_11B| WIFI_PROTOCOL_11G|WIFI_PROTOCOL_11N));

    ESP_LOGI(TAG, "configure_wifi finished.");
}



//Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/wifi.html#wi-fi-start-phase
void start_wifi(void)
{   
    /* The Wi-Fi driver posts WIFI_EVENT_STA_START event to the defualt event loop(created above); 
    then, the default event loop will do some common things and will call the 
    user event handler(callback function).For that, first, esp_event_handler_instance_register() 
    registers an instance of the user event handler to the default event loop.
    This must be done before WiFi driver is started in next step so that user event handler
    is ready to receive events. */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                     &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                        &event_handler, NULL, NULL));

    //Start WiFi driver according to current configuration
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wifi started.");                    
}

//Refer to https://docs.espressif.com/projects/esp-idf/en/release-v4.0/api-reference/provisioning/wifi_provisioning.html#start-provisioning-service
void start_wifi_provisioning(void)
{

    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    
    
    /*The Wi-Fi provision manager posts WIFI_PROV_EVENT event to the
    defualt event loop(created above); 
    then, the default event loop will do some common things and will call the 
    user event handler(callback function)which de-initialize the  provision manager to free up 
    resources.For that, first, esp_event_handler_instance_register() 
    registers an instance of the user event handler to the default event loop.
    This must be done before WiFi provision manager is started in next step so that 
    user event handler is ready to receive events. */
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                    &event_handler, NULL, NULL));

    /* The provisioning service will automatically finish only if it receives valid Wi-Fi AP
    credentials followed by successfully connection of device to the AP (working as STA).
    This api saves the received wifi credentials in NVS automatically. */
    ESP_ERROR_CHECK( wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key) );
}

void app_main(void)
{   
    //Setting BOOT button  to erase nvs flash if pressed for more than 3 seconds(Interrupt based)
    btn_driver_init();
    
    // NVS, TCP/IP, Event Loop and Wi-FI should be initialised first before provisioning
    //This API initialises the default NVS partition. The default NVS partition is the one
    //that is labeled “nvs” in the default partition table.
    //Refer to https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html#_CPPv414nvs_flash_initv
    nvs_init();
    //Init wifi in Access point mode
    init_wifi();

    ESP_LOGI(TAG, "Init provisioning");
    init_wifi_provisioning();
    /* Let's find out if the device is already provisioned. It checks checks if Wi-Fi credentials 
    are already present on the NVS. Refer to https://docs.espressif.com/projects/esp-idf/en/release-v4.0/api-reference/provisioning/wifi_provisioning.html#_CPPv428wifi_prov_mgr_is_provisionedPb */
    bool provisioned = false;
    ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned)); //Set provisioned=true if wifi credentials present
    if (!provisioned) 
    {   
        //When using SoftAP transport, for allowing service discovery, mDNS must be initialized before starting provisioning
        start_mdns_service();
        ESP_LOGI(TAG, "Starting provisioning");
        start_wifi_provisioning();
    }
    else 
    {   
        ESP_LOGI(TAG, "Wifi configuration already stored in flash partition called NVS");
        wifi_config_t conf;
        ESP_ERROR_CHECK(esp_wifi_get_config(ESP_IF_WIFI_STA, &conf));
        ESP_LOGI(TAG, "%s" ,conf.sta.ssid);
        ESP_LOGI(TAG, "%s" ,conf.sta.password);
        ESP_LOGI(TAG, "Starting Wi-Fi STA");    
         /* We don't need the manager as device is already provisioned,
         * so let's release it's resources */
        wifi_prov_mgr_deinit();
        configure_wifi();
        start_wifi();    
    }

    ESP_LOGI(TAG, "Waiting for the chip to connect to wifi");  
    //waiting for the bit number 0 to be set to 1.
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
    aws_connect_init();
}