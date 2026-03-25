/*
 * ESP32-S3 Sesli Asistan
 *
 * Mikrofon -> Whisper STT -> Groq AI -> Edge-TTS -> Hoparlor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"

static const char *TAG = "JARVIS";

static float g_sicaklik = -999;
static float g_nem = -999;

/* ===================== AYARLAR ===================== */

// Wi-Fi
#define WIFI_SSID           "WIFI_ADINIZI_BURAYA_YAZIN"
#define WIFI_PASS           "WIFI_SIFRENIZI_BURAYA_YAZIN"

// Bilgisayardaki server.py adresi
#define STT_URL             "http://SUNUCU_IP_ADRESI:5000/stt"
#define TTS_URL             "http://SUNUCU_IP_ADRESI:5000/tts"

// Mikrofon pinleri (INMP441)
#define MIC_BCK_PIN         16
#define MIC_WS_PIN          15
#define MIC_DIN_PIN         17

// Hoparlor pinleri (MAX98357A veya benzeri I2S amplifikatör)
#define SPK_BCK_PIN         4
#define SPK_WS_PIN          5
#define SPK_DOUT_PIN        6

// LED pini
#define LED_PIN             7

// DHT11 sicaklik sensoru pini
#define DHT11_PIN           8

// Ses kayit ayarlari
#define SAMPLE_RATE         16000
#define RECORD_SECONDS      5
#define SILENCE_THRESHOLD   150
#define HTTP_TIMEOUT_MS     30000

/* ===================== DAHILI ===================== */

#define I2S_MIC_PORT        I2S_NUM_0
#define I2S_SPK_PORT        I2S_NUM_1
#define WIFI_CONNECTED_BIT  BIT0
#define TOTAL_SAMPLES       (SAMPLE_RATE * RECORD_SECONDS)

static EventGroupHandle_t g_wifi_events = NULL;

/* ---------- MIKROFON KURULUMU ---------- */

static void mikrofon_baslat(void)
{
    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_RX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false
    };

    i2s_pin_config_t pins = {
        .bck_io_num = MIC_BCK_PIN,
        .ws_io_num = MIC_WS_PIN,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = MIC_DIN_PIN
    };

    i2s_driver_install(I2S_MIC_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_MIC_PORT, &pins);
    i2s_zero_dma_buffer(I2S_MIC_PORT);
    ESP_LOGI(TAG, "Mikrofon hazir (BCK=%d, WS=%d, DIN=%d)",
             MIC_BCK_PIN, MIC_WS_PIN, MIC_DIN_PIN);
}

/* ---------- HOPARLOR KURULUMU ---------- */

static void hoparlor_baslat(void)
{
    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 1024,
        .use_apll = false
    };

    i2s_pin_config_t pins = {
        .bck_io_num   = SPK_BCK_PIN,
        .ws_io_num    = SPK_WS_PIN,
        .data_out_num = SPK_DOUT_PIN,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_SPK_PORT, &cfg, 0, NULL);
    i2s_set_pin(I2S_SPK_PORT, &pins);
    i2s_zero_dma_buffer(I2S_SPK_PORT);
    ESP_LOGI(TAG, "Hoparlor hazir (BCK=%d, WS=%d, DOUT=%d)",
             SPK_BCK_PIN, SPK_WS_PIN, SPK_DOUT_PIN);
}

/* ---------- LED KONTROL ---------- */

static void led_baslat(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(LED_PIN, 0);
    ESP_LOGI(TAG, "LED hazir (GPIO %d)", LED_PIN);
}

static void komut_isle(const char *cmd)
{
    if (strcmp(cmd, "ISIK_AC") == 0) {
        gpio_set_level(LED_PIN, 1);
        ESP_LOGI(TAG, ">>> ISIK ACILDI");
    } else if (strcmp(cmd, "ISIK_KAPAT") == 0) {
        gpio_set_level(LED_PIN, 0);
        ESP_LOGI(TAG, ">>> ISIK KAPATILDI");
    }
}

/* ---------- DHT11 SICAKLIK SENSORU ---------- */

/* --- DHT11 one-wire okuma --- */

static int dht11_oku(float *sicaklik, float *nem)
{
    uint8_t veri[5] = {0};

    gpio_pullup_en(DHT11_PIN);
    gpio_pulldown_dis(DHT11_PIN);

    // Baslat sinyali: OUTPUT, LOW 20ms
    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    // Serbest birak - INPUT'a gec
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&mux);

    // Sensor hizli yanitlarsa pin zaten LOW olabilir
    int t = 0;
    if (gpio_get_level(DHT11_PIN) == 1) {
        while (gpio_get_level(DHT11_PIN) == 1 && t < 500) { ets_delay_us(1); t++; }
        if (t >= 500) { portEXIT_CRITICAL(&mux); return -1; }
    }

    // Sensor LOW fazi (~80us)
    t = 0;
    while (gpio_get_level(DHT11_PIN) == 0 && t < 500) { ets_delay_us(1); t++; }
    if (t >= 500) { portEXIT_CRITICAL(&mux); return -2; }

    // Sensor HIGH fazi (~80us)
    t = 0;
    while (gpio_get_level(DHT11_PIN) == 1 && t < 500) { ets_delay_us(1); t++; }
    if (t >= 500) { portEXIT_CRITICAL(&mux); return -3; }

    // 40 bit veri
    for (int i = 0; i < 40; i++) {
        t = 0;
        while (gpio_get_level(DHT11_PIN) == 0 && t < 500) { ets_delay_us(1); t++; }
        if (t >= 500) { portEXIT_CRITICAL(&mux); return -4; }

        int high = 0;
        while (gpio_get_level(DHT11_PIN) == 1 && high < 500) { ets_delay_us(1); high++; }
        if (high >= 500) { portEXIT_CRITICAL(&mux); return -5; }

        veri[i / 8] <<= 1;
        if (high > 40) veri[i / 8] |= 1;
    }

    portEXIT_CRITICAL(&mux);

    uint8_t toplam = veri[0] + veri[1] + veri[2] + veri[3];
    if (toplam != veri[4]) {
        ESP_LOGW(TAG, "DHT11 checksum HATA: [%d %d %d %d %d]",
                 veri[0], veri[1], veri[2], veri[3], veri[4]);
        return -6;
    }

    *nem = veri[0] + veri[1] * 0.1f;
    *sicaklik = veri[2] + veri[3] * 0.1f;
    return 0;
}

static void sicaklik_guncelle(void)
{
    float s = 0, n = 0;
    int sonuc = dht11_oku(&s, &n);
    if (sonuc == 0) {
        g_sicaklik = s;
        g_nem = n;
        ESP_LOGI(TAG, "DHT11 OK: %.1f C, %%%.1f nem", s, n);
    } else {
        ESP_LOGW(TAG, "DHT11 hata=%d", sonuc);
    }
}

/* ---------- WI-FI ---------- */

static void wifi_olay_cb(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(g_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(g_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static bool wifi_baglan(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    g_wifi_events = xEventGroupCreate();
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                        &wifi_olay_cb, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                        &wifi_olay_cb, NULL, NULL);

    wifi_config_t sta = {0};
    strncpy((char *)sta.sta.ssid, WIFI_SSID, sizeof(sta.sta.ssid) - 1);
    strncpy((char *)sta.sta.password, WIFI_PASS, sizeof(sta.sta.password) - 1);
    sta.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* Uyku modu TCP kesilmelerini artirabilir; HTTP/STT icin kapat */
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    ESP_LOGI(TAG, "Wi-Fi'a baglaniliyor: %s", WIFI_SSID);
    EventBits_t bits = xEventGroupWaitBits(g_wifi_events,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(20000));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi baglantisi basarisiz!");
        return false;
    }
    ESP_LOGI(TAG, "Wi-Fi baglandi.");
    return true;
}

/* ---------- JSON PARSE ---------- */

static bool json_deger_al(const char *json, const char *anahtar,
                           char *out, size_t len)
{
    char arama[64];
    snprintf(arama, sizeof(arama), "\"%s\"", anahtar);

    const char *key = strstr(json, arama);
    if (!key) return false;

    const char *colon = strchr(key + strlen(arama), ':');
    if (!colon) return false;

    const char *q1 = strchr(colon, '\"');
    if (!q1) return false;
    q1++;

    const char *q2 = strchr(q1, '\"');
    if (!q2 || q2 <= q1) return false;

    size_t n = (size_t)(q2 - q1);
    if (n >= len) n = len - 1;
    memcpy(out, q1, n);
    out[n] = '\0';
    return true;
}

/* ---------- SES -> HTTP -> METIN ---------- */

static bool sesi_sunucuya_gonder(const int16_t *pcm, size_t sample_sayisi,
                                  char *metin, size_t metin_len,
                                  char *ai_yanit, size_t ai_yanit_len,
                                  char *komut, size_t komut_len)
{
    esp_http_client_config_t cfg = {
        .url = STT_URL,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return false;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
    esp_http_client_set_header(client, "X-Sample-Rate", "16000");

    char sicaklik_str[16], nem_str[16];
    snprintf(sicaklik_str, sizeof(sicaklik_str), "%.1f", g_sicaklik);
    snprintf(nem_str, sizeof(nem_str), "%.1f", g_nem);
    esp_http_client_set_header(client, "X-Temperature", sicaklik_str);
    esp_http_client_set_header(client, "X-Humidity", nem_str);

    int veri_boyutu = (int)(sample_sayisi * sizeof(int16_t));

    esp_err_t err = esp_http_client_open(client, veri_boyutu);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP baglanti hatasi: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_write(client, (const char *)pcm, veri_boyutu);
    esp_http_client_fetch_headers(client);

    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "HTTP hata kodu: %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    char yanit[2048] = {0};
    int toplam = 0, n;
    while ((n = esp_http_client_read(client, yanit + toplam,
                                      sizeof(yanit) - 1 - toplam)) > 0) {
        toplam += n;
        if (toplam >= (int)sizeof(yanit) - 1) break;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (toplam <= 0) return false;
    yanit[toplam] = '\0';

    json_deger_al(yanit, "text", metin, metin_len);

    if (ai_yanit && ai_yanit_len > 0) {
        json_deger_al(yanit, "reply", ai_yanit, ai_yanit_len);
    }
    if (komut && komut_len > 0) {
        json_deger_al(yanit, "command", komut, komut_len);
    }

    return (strlen(metin) > 0);
}

/* ---------- TTS: METIN -> HOPARLOR ---------- */

static void yanitı_seslendir(const char *metin)
{
    ESP_LOGI(TAG, "TTS basliyor...");

    esp_http_client_config_t cfg = {
        .url = TTS_URL,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return;

    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type",
                               "text/plain; charset=utf-8");

    int body_len = (int)strlen(metin);
    esp_err_t err = esp_http_client_open(client, body_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TTS baglanti hatasi: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    esp_http_client_write(client, metin, body_len);
    esp_http_client_fetch_headers(client);

    int status = esp_http_client_get_status_code(client);
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "TTS HTTP hata: %d", status);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return;
    }

    char buf[4096];
    int n;
    int toplam = 0;
    while ((n = esp_http_client_read(client, buf, sizeof(buf))) > 0) {
        size_t written = 0;
        i2s_write(I2S_SPK_PORT, buf, n, &written, portMAX_DELAY);
        toplam += n;
    }

    i2s_zero_dma_buffer(I2S_SPK_PORT);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "TTS tamamlandi (%d bayt ses calindi)", toplam);
}

/* ---------- ANA DINLEME DONGUSU ---------- */

static void Dinleme_Gorevi(void *pv)
{
    int16_t *kayit_buf = heap_caps_malloc(TOTAL_SAMPLES * sizeof(int16_t),
                                           MALLOC_CAP_8BIT);
    int32_t *i2s_buf   = heap_caps_malloc(512 * sizeof(int32_t),
                                           MALLOC_CAP_8BIT);
    if (!kayit_buf || !i2s_buf) {
        ESP_LOGE(TAG, "Bellek yetersiz!");
        free(kayit_buf);
        free(i2s_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Dinliyorum... (%d sn aralikla)", RECORD_SECONDS);

    while (1) {
        int dolu = 0;
        long long enerji = 0;

        while (dolu < TOTAL_SAMPLES) {
            size_t okunan = 0;
            i2s_read(I2S_MIC_PORT, i2s_buf, 512 * sizeof(int32_t),
                     &okunan, portMAX_DELAY);
            int adet = (int)(okunan / sizeof(int32_t));
            for (int i = 0; i < adet && dolu < TOTAL_SAMPLES; i++) {
                int16_t s = (int16_t)(i2s_buf[i] >> 12);
                kayit_buf[dolu++] = s;
                enerji += llabs((long long)s);
            }
        }

        int ort = (int)(enerji / TOTAL_SAMPLES);
        if (ort < SILENCE_THRESHOLD) {
            continue;  // sessiz, atla
        }

        ESP_LOGI(TAG, "Ses algilandi (seviye=%d), sunucuya gonderiliyor...", ort);

        sicaklik_guncelle();

        char metin[256] = {0};
        char ai_yanit[512] = {0};
        char komut[32] = {0};
        if (sesi_sunucuya_gonder(kayit_buf, TOTAL_SAMPLES,
                                  metin, sizeof(metin),
                                  ai_yanit, sizeof(ai_yanit),
                                  komut, sizeof(komut))
            && strlen(metin) > 0) {
            ESP_LOGI(TAG, "[SEN] %s", metin);

            if (strlen(komut) > 0) {
                ESP_LOGI(TAG, "[CMD] %s", komut);
                komut_isle(komut);
            } else {
                ESP_LOGW(TAG, "Komut yok");
            }

            if (strlen(ai_yanit) > 0) {
                ESP_LOGI(TAG, "[AI]  %s", ai_yanit);
                yanitı_seslendir(ai_yanit);
            }
        } else {
            ESP_LOGW(TAG, "Metin alinamadi.");
        }
    }
}

/* ---------- BASLANGIC ---------- */

void app_main(void)
{
    ESP_LOGI(TAG, "Sesli Asistan basliyor...");

    mikrofon_baslat();
    hoparlor_baslat();
    led_baslat();
    // DHT11 sicaklik sensoru baslatma
    vTaskDelay(pdMS_TO_TICKS(2000));
    sicaklik_guncelle();
    ESP_LOGI(TAG, "DHT11 hazir (GPIO %d)", DHT11_PIN);

    // LED baglanti testi - 3 kez yanip soner
    ESP_LOGI(TAG, "LED test basliyor...");
    for (int i = 0; i < 3; i++) {
        gpio_set_level(LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    ESP_LOGI(TAG, "LED test bitti.");

    if (!wifi_baglan()) {
        ESP_LOGE(TAG, "Wi-Fi olmadan devam edilemiyor.");
        return;
    }

    xTaskCreatePinnedToCore(Dinleme_Gorevi, "STT", 12288, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "Sistem hazir. Mikrofona konusun!");
}
