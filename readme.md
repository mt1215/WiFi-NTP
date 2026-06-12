# ESP32-S3 多組 Wi-Fi 配置與高效網路時間同步系統 (SNTP/RTC)

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0.1-green.svg)](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/)
[![Platform](https://img.shields.io/badge/Platform-ESP32--S3-orange.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Language](https://img.shields.io/badge/Language-C-blue.svg)]()

本專案基於 **ESP-IDF v6.0.1** 穩定版開發框架，實現了一套可動態於 `menuconfig` 選單中切換多組測試 Wi-Fi 熱點（SSID/Password）的嵌入式系統。系統整合了新版 `esp_netif_sntp` 時間元件，能全自動與台灣標準時間（NTP 伺服器）完成高精準度對時，並自動校正時區（CST-8），同時內部具備底層斷線 Reason Code 診斷機制與全時射頻優化，可作為工業物聯網或邊緣運算裝置的網路基礎通訊範本。

---

## 🎯 專案核心特點

1. **Kconfig 條件式動態編譯**：免動原始碼！直接在 `idf.py menuconfig` 視覺化選單中挑選要測試的 Wi-Fi 基地台，C 語言底層透過預處理器（Preprocessor Macros）自動載入對應帳密。
2. **新版 SNTP 核心與 CST-8 時區校正**：採用 ESP-IDF 最新標準實作，對時成功後自動綁定台灣標準時間（包含 `+8` 小時校正），主程式隨後可經由硬體 RTC 持續追蹤精確秒數。
3. **Reason Code 底層斷線診斷**：針對 Wi-Fi 複雜環境，即時攔截並解析底層錯誤代碼（例如樂鑫擴充碼 `WIFI_REASON_BEACON_TIMEOUT = 200` 等），提高維護與除錯效率。

---

## 📂 專案檔案結構

```text
├── CMakeLists.txt          # 元件註冊與編譯依賴宣告 (需依賴 esp_wifi, esp_netif 等)
├── Kconfig.projbuild       # 視覺化選單配置檔 (定義 Tom 多組 Wi-Fi 測試配置)
└── main
    └── RTC.c               # 核心主程式 (包含 Wi-Fi 驅動、SNTP 任務與 RTC 循環)
