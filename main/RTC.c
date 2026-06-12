#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif_sntp.h"
#include "lwip/ip_addr.h"

// 引入從 sdkconfig 自動生成的巨集
#include "sdkconfig.h"

static const char *TAG = "Tom_WiFi_Direct_IP";

/* FreeRTOS 事件組 */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

// ==========================================
// 🕒 台灣時區與 SNTP 初始化函式
// ==========================================
void initialize_sntp(void)
{
    ESP_LOGI(TAG, "⏳ 初始化 SNTP 服務，準備與網路時間同步...");

    // 1. 設定新版 SNTP 配置結構體
    // 使用台灣常用的 NTP 伺服器 (中華電信與 time.stdtime.gov.tw)
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("tock.stdtime.gov.tw");
    
    // 2. 啟動服務
    esp_netif_sntp_init(&config);

    // 3. 🛠️ 關鍵設定：設定台灣時區 (CST-8)
    // 這樣後面用 localtime() 轉換時，才會自動加上 8 小時，而不是顯示格林威治時間 (UTC)
    setenv("TZ", "CST-8", 1);
    tzset();
}

// ==========================================
// 🕒 阻塞等待時間同步並列印結果
// ==========================================
void wait_for_time_sync(void)
{
    int retry = 0;
    const int retry_count = 15;
    
    initialize_sntp();

    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(1000)) != ESP_OK && ++retry < retry_count) {
        ESP_LOGW(TAG, "⏳ 等待網路時間同步中... (%d/%d)", retry, retry_count);
    }

    // 取得當前系統時間
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    // 檢查有沒有同步成功（如果年份大於 1970 代表成功了）
    if (timeinfo.tm_year < (1970 - 1900)) {
        ESP_LOGE(TAG, "❌ 時間同步失敗，請檢查 NTP 伺服器或網路路由！");
    } else {
        char str_buf[64];
        strftime(str_buf, sizeof(str_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "🎉 時間同步成功！當前台灣標準時間: %s", str_buf);
    }
}

// ==========================================
// 💡 Wi-Fi 事件回呼處理器
// ==========================================
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } 
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGE(TAG, "❌ Wi-Fi 連線中斷！Reason Code: %d", disconnected->reason);

        // 如果連線中斷時正在連線，直接重新呼叫
        if (disconnected->reason != 203) { // 203 是 sta is connecting 狀態衝突，跳過重試以避免 Log 爆炸
            if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGW(TAG, "自動重新連線中... (%d/%d)", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY);
            } else {
                xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            }
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "🎉 成功取得 IP 位址: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ==========================================
// 📡 Wi-Fi 周邊熱點掃描函式
// ==========================================
void wifi_scan_neighbors(void)
{
    ESP_LOGI(TAG, "--------------------------------------------------");
    ESP_LOGI(TAG, "📡 開始掃描周邊 Wi-Fi 熱點...");

    wifi_scan_config_t scan_config = {
        .ssid = NULL, .bssid = NULL, .channel = 0, .show_hidden = false
    };

    esp_wifi_scan_start(&scan_config, true);

    uint16_t number = 20;
    wifi_ap_record_t ap_info[20];
    uint16_t ap_count = 0;

    esp_wifi_scan_get_ap_records(&number, ap_info);
    esp_wifi_scan_get_ap_num(&ap_count);

    for (int i = 0; i < ap_count && i < 20; i++) {
        ESP_LOGI(TAG, "[%02d] | %-32s | %-4d | %d dBm", 
                 i + 1, ap_info[i].ssid, ap_info[i].primary, ap_info[i].rssi);
    }
    ESP_LOGI(TAG, "--------------------------------------------------");
}

// ==========================================
// 🚀 Wi-Fi 核心初始化與連線主程式
// ==========================================
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan_neighbors();

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {.capable = true, .required = false},
        },
    };

    #if defined(CONFIG_USE_AP_1)
        strcpy((char *)wifi_config.sta.ssid, CONFIG_TEST_WIFI_SSID_1);
        strcpy((char *)wifi_config.sta.password, CONFIG_TEST_WIFI_PASS_1);
        ESP_LOGW(TAG, "⚙️ 偵測到 Kconfig 勾選 【AP 1】，載入目標：%s", CONFIG_TEST_WIFI_SSID_1);
    #elif defined(CONFIG_USE_AP_2)
        strcpy((char *)wifi_config.sta.ssid, CONFIG_TEST_WIFI_SSID_2);
        strcpy((char *)wifi_config.sta.password, CONFIG_TEST_WIFI_PASS_2);
        ESP_LOGW(TAG, "⚙️ 偵測到 Kconfig 勾選 【AP 2】，載入目標：%s", CONFIG_TEST_WIFI_SSID_2);
    #elif defined(CONFIG_USE_AP_3)
        strcpy((char *)wifi_config.sta.ssid, CONFIG_TEST_WIFI_SSID_3);
        strcpy((char *)wifi_config.sta.password, CONFIG_TEST_WIFI_PASS_3);
        ESP_LOGW(TAG, "⚙️ 偵測到 Kconfig 勾選 【AP 3】，載入目標：%s", CONFIG_TEST_WIFI_SSID_3);
    #endif

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_LOGI(TAG, "🔗 正在嘗試連線至熱點: %s ...", wifi_config.sta.ssid);
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, pdFALSE, portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "✅ Wi-Fi 成功就緒！開始進行網路時間同步...");
        // 💡 核心重點：Wi-Fi 確定通了，才進去拿網路時間
        wait_for_time_sync();
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "❌ 已達到最大重試次數，連線宣告失敗。");
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();

    // 保持主程式每 10 秒印出一次當前時間，驗證內部 RTC 有在跳動
    while (1) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        
        char str_buf[64];
        strftime(str_buf, sizeof(str_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI("CLOCK", "🕒 當前系統時間: %s", str_buf);
        
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}