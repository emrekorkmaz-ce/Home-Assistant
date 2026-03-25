# Jarvis - ESP32 Voice Assistant (Home Assistant)

[🇹🇷 Türkçe Sürümü İçin Aşağıya Kaydırın](#türkçe-sürüm-turkish-version)

This project is a Turkish-speaking smart home voice assistant (Jarvis) powered by an **ESP32-S3**. The system consists of a Python server running on your local network and C (ESP-IDF) firmware running on the microcontroller.

## Features
- **Voice Command:** Listens to ambient sound using an INMP441 I2S microphone. The audio is instantly transcribed to text (STT) via the [Groq Whisper API](https://groq.com/).
- **Artificial Intelligence (AI):** Uses the LLaMA 3.3 70B Versatile model to generate concise, localized Turkish responses to your questions.
- **Text-to-Speech (TTS):** Converts text into a natural human voice with Microsoft Edge TTS, played through an I2S speaker (MAX98357A).
- **Smart Home Control:** Can turn LEDs or relays on/off on the ESP32 with voice commands like "Turn on the light".
- **Sensor Reading:** Measures room temperature and humidity using a DHT11 sensor and reports it verbally when asked.

## Hardware Requirements
- ESP32-S3 Development Board
- INMP441 I2S Microphone
- MAX98357A I2S Amplifier + Speaker
- DHT11 Temperature and Humidity Sensor
- LED (or Relay Module)

## Software Setup & Usage

### 1- Server (PC) Setup
First, you need to run the Python server which acts as the brain of the assistant.
1. Make sure Python is installed on your computer.
2. Install the required libraries: `pip install flask httpx edge-tts miniaudio`
3. Create a `.env` file (or set an environment variable) and enter your Groq API key: `GROQ_API_KEY=gsk_your_key_here...`
4. Start the server by running `python server.py` (or double-click the `sunucu_baslat.bat` file if you're on Windows).
5. You will see an output like `Server: http://192.168.1.XXX:5000` in the console. Copy this local network IP address.

### 2- ESP32 Setup
1. Open the `main/main.c` file.
2. Enter your Wi-Fi name and password in the `WIFI_SSID` and `WIFI_PASS` sections.
3. Replace the IP address in the `STT_URL` and `TTS_URL` constants with your computer's local IP address you copied earlier.
4. Open the ESP-IDF terminal, build, and flash the project:
   ```bash
   idf.py set-target esp32s3
   idf.py build
   idf.py -p COM3 flash monitor  # (Change COM3 to your actual port)
   ```

### 3- How It Works
When both systems are running, the ESP32 connects to the computer on your local network over Wi-Fi. 
When you speak into the mic, the audio is recorded and sent to the Python server (`/stt`). The Python server sends the audio to Groq to understand it, interprets it with AI, and formulates a response. Then, the ESP32 connects to the server to fetch the audio file of the response (`/tts`) and plays it for you through the speaker.

If you ask "What is the room temperature?", the ESP32 reads the instantaneous value from the DHT11 and shares it with the server, and Jarvis tells you the ambient temperature and humidity verbally.

---
**Security Note:**
Make sure you replace the placeholder Wi-Fi credentials and do not push your real API keys or passwords directly to GitHub in `server.py` or `main.c`.

<br>
<br>

---

# [Türkçe Sürüm / Turkish Version]

Bu proje, **ESP32-S3** kullanan Türkçe konuşan akıllı bir ev sesli asistanıdır (Jarvis). Sistem, yerel ağınızda çalışan bir Python sunucusu ve mikrodenetleyici üzerindeki C (ESP-IDF) yazılımından oluşur.

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
