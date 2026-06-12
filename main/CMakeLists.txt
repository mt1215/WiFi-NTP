idf_component_register(SRCS "RTC.c"
                    INCLUDE_DIRS "."
					REQUIRES esp_wifi esp_netif esp_event nvs_flash esp_driver_gpio)
