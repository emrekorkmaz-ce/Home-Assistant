# Jarvis - ESP32 Voice Assistant (Home Assistant)

Bu proje, **ESP32-S3** kullanan Türkçe konuşan bir sesli ev asistanıdır (Jarvis). Sistem, yerel ağınızda çalışan bir Python sunucusu ve mikrodenetleyici üzerindeki C (ESP-IDF) yazılımından oluşur.

## Özellikler
- **Sesli Komut:** INMP441 I2S mikrofon ile ortamdaki sesi dinler. Ses [Groq Whisper API](https://groq.com/) aracılığıyla anında metne çevrilir (STT).
- **Yapay Zeka (AI):** LLaMA 3.3 70B modeli kullanılarak sorulan sorulara Türkçe ve kısa yanıtlar üretilir.
- **Seslendirme (TTS):** Microsoft Edge TTS sayesinde metinler doğal bir Türkçe insan sesiyle I2S hoparlörden (MAX98357A) oynatılır.
- **Akıllı Ev Kontrolü:** "Işığı aç", "Işığı kapat" gibi komutlarla ESP32 üzerindeki LED veya röleler kontrol edilir.
- **Sensör Okuma:** DHT11 sensörü kullanılarak odadaki sıcaklık ve nem ölçülür ve sorulduğunda sesli olarak size bildirilir.

## Donanım Gereksinimleri
- ESP32-S3 Geliştirme Kartı
- INMP441 I2S Mikrofon 
- MAX98357A I2S Amfi + Hoparlör
- DHT11 Sıcaklık ve Nem Sensörü
- LED (veya Röle Modülü)

## Yazılım Kurulumu ve Kullanımı

### 1- Sunucu (Bilgisayar) Kurulumu
Öncelikle asistanın beyin takımını oluşturan Python sunucusunu çalıştırmalısınız.
1. Bilgisayarınızda Python yüklü olduğundan emin olun.
2. Gerekli kütüphaneleri yükleyin: `pip install flask httpx edge-tts miniaudio`
3. Bir `.env` dosyası oluşturun (veya ortam değişkeni olarak ekleyin) ve Groq API anahtarınızı girin: `GROQ_API_KEY=gsk_sizin_anahtariniz...`
4. Sunucuyu başlatmak için proje dizinindeki **`sunucu_baslat.bat`** dosyasına çift tıklayın veya `python server.py` çalıştırın.
5. Konsolda `Sunucu: http://192.168.1.XXX:5000` benzeri bir çıktı göreceksiniz. Bilgisayarınızın bu yerel ağ IP'sini kopyalayın.

### 2- ESP32 Kurulumu
1. `main/main.c` dosyasını açın.
2. `WIFI_SSID` ve `WIFI_PASS` kısımlarına kendi Wi-Fi adınızı ve şifrenizi yazın.
3. `STT_URL` ve `TTS_URL` sabitlerinde yer alan IP adresini, az önce kopyaladığınız bilgisayarınızın yerel IP adresiyle değiştirin.
4. ESP-IDF terminalini açın ve projeyi derleyip yükleyin:
   ```bash
   idf.py set-target esp32s3
   idf.py build
   idf.py -p COM3 flash monitor  # (COM3'ü kendi portunuza göre değiştirin)
   ```

### 3- Nasıl Çalışır?
Her iki sistem de çalıştığında ESP32, Wi-Fi üzerinden yerel ağınızdaki bilgisayara bağlanır. 
Siz mikrofona konuştuğunuzda ses kaydedilir ve Python sunucusuna (`/stt`) yollanır. Python sunucusu sesi Groq'a yollayıp anlar, yapay zekaya yorumlatır, cevap oluşturur. Ardından ESP32 cevabın ses dosyasını çekmek için (`/tts`) sunucuya bağlanır ve hoparlörden yanıtı size dinletir.

Eğer "Odanın sıcaklığı kaç derece?" derseniz, ESP32 DHT11'den anlık değeri okuyup sunucuyla paylaşır, Jarvis de size ortamın durumunu söyler.

---
**Güvenlik Notu:**
`server.py` içerisinde veya `main.c` içerisinde gerçek şifrelerinizi ve API anahtarlarınızı GitHub'a açık şekilde yüklememeye dikkat edin.
