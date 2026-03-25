"""
ESP32 Sesli Asistan Sunucusu

  Mikrofon -> Groq Whisper STT -> Groq AI -> Edge-TTS -> Hoparlor
"""

import io
import wave
import socket
import asyncio

import httpx
import miniaudio
import edge_tts
from flask import Flask, request, jsonify, Response

app = Flask(__name__)
app.json.ensure_ascii = False

# ---- AYARLAR ----
import os
GROQ_API_KEY = os.environ.get("GROQ_API_KEY", "BURAYA_KENDI_API_ANAHTARINIZI_YAZIN")
GROQ_MODEL = "llama-3.3-70b-versatile"
TTS_VOICE = "tr-TR-AhmetNeural"
TTS_SAMPLE_RATE = 16000

SYSTEM_PROMPT = (
    "Sen Jarvis adında bir Türkçe sesli asistansın. Tek cümle yanıt ver. ASLA soru sorma.\n"
    "Yalnızca Türkçe yaz; başka dil veya karakter kullanma.\n\n"
    "Kullanıcı LED/ışık/lamba açmak isterse sadece şunu de: 'LED'i açıyorum!'\n"
    "Kullanıcı LED/ışık/lamba kapatmak isterse sadece şunu de: 'LED'i kapatıyorum!'\n\n"
    "ÖNEMLİ: Kullanıcı mesajında [Sensör: ...] bilgisi varsa bu sadece oda sıcaklığı/nem "
    "sorusu için verilmiştir; o zaman bu değerleri söyle.\n"
    "[Sensör: ...] YOKSA veya kullanıcı sıcaklık/nem sormadıysa asla oda sıcaklığı veya "
    "nemden bahsetme; sadece sorulan konuya kısa yanıt ver."
)


def kullanici_sicaklik_nem_soruyor_mu(metin: str) -> bool:
    """Sadece bu durumda sensör verisini modele ekleriz."""
    if not metin:
        return False
    t = metin.lower()
    # Türkçe karakter varyasyonları
    t = t.replace("ı", "i").replace("ğ", "g").replace("ü", "u").replace("ş", "s")
    t = t.replace("ö", "o").replace("ç", "c")
    anahtarlar = (
        "sicaklik", "sicak", "soguk", "nem", "rutubet",
        "derece", "oda sicak", "oda nem", "odanin sicak", "odanin nem",
        "odada sicak", "odada nem", "hava sicak", "hava soguk",
        "sensor", "sicak mi", "soguk mu", "kac derece",
        "termostat", "isitma", "havalandirma",
    )
    return any(k in t for k in anahtarlar)


# ---- GROQ AI ----
def groq_yanit_al(kullanici_metni):
    try:
        r = httpx.post(
            "https://api.groq.com/openai/v1/chat/completions",
            headers={
                "Authorization": f"Bearer {GROQ_API_KEY}",
                "Content-Type": "application/json",
            },
            json={
                "model": GROQ_MODEL,
                "messages": [
                    {"role": "system", "content": SYSTEM_PROMPT},
                    {"role": "user", "content": kullanici_metni},
                ],
                "max_tokens": 150,
                "temperature": 0.7,
            },
            timeout=15.0,
        )
        r.raise_for_status()
        return r.json()["choices"][0]["message"]["content"].strip()
    except Exception as e:
        print(f"[HATA] Groq: {e}")
        return ""


# ---- TTS (Edge-TTS -> PCM) ----
async def _edge_tts_mp3(text):
    communicate = edge_tts.Communicate(text, TTS_VOICE)
    mp3_parts = []
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            mp3_parts.append(chunk["data"])
    return b"".join(mp3_parts)


def metin_to_pcm(text):
    mp3_data = asyncio.run(_edge_tts_mp3(text))
    decoded = miniaudio.decode(
        mp3_data,
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=1,
        sample_rate=TTS_SAMPLE_RATE,
    )
    return bytes(decoded.samples)


# ---- KOMUT ALGILAMA ----
def _metin_normalize(metin: str) -> str:
    m = metin.lower()
    for a, b in (
        ("ı", "i"),
        ("ğ", "g"),
        ("ü", "u"),
        ("ş", "s"),
        ("ö", "o"),
        ("ç", "c"),
    ):
        m = m.replace(a, b)
    return m


def _ac_niyeti_var_mi(s: str) -> bool:
    """'ac' harfleri baska kelimelerde (or. sac) yanlis tetiklemesin diye."""
    if "yak" in s or "calistir" in s:
        return True
    if "aciyor" in s or "aciyorum" in s or "acik" in s or "actim" in s or "actik" in s:
        return True
    if " ac " in s or s.startswith("ac ") or " aci" in s:
        return True
    return False


def _kapat_niyeti_var_mi(s: str) -> bool:
    return any(k in s for k in ["kapat", "sondur", "durdur", "kapaticam", "kapatalim"])



def komut_algila(text, reply):
    """Kullanici metni ve AI yanitindan LED komutunu tespit et."""
    if "[ISIK_AC]" in reply:
        return "ISIK_AC", reply.replace("[ISIK_AC]", "").strip()
    if "[ISIK_KAPAT]" in reply:
        return "ISIK_KAPAT", reply.replace("[ISIK_KAPAT]", "").strip()

    repl = _metin_normalize(reply)
    txt = _metin_normalize(text)

    ac_r = _ac_niyeti_var_mi(repl)
    kapat_r = _kapat_niyeti_var_mi(repl)
    ac_t = _ac_niyeti_var_mi(txt)
    kapat_t = _kapat_niyeti_var_mi(txt)
    ac = ac_r or ac_t
    kapat = kapat_r or kapat_t

    cihaz = any(k in (repl + " " + txt) for k in ["isik", "isig", "lamba", "led"])

    # AI yaniti
    if cihaz and (
        "led" in repl
        or "lamba" in repl
        or "isik" in repl
        or "isig" in repl
    ):
        if kapat_r:
            return "ISIK_KAPAT", reply
        if ac_r:
            return "ISIK_AC", reply

    # STT + yanit
    if cihaz and (ac or kapat):
        if kapat:
            return "ISIK_KAPAT", reply
        if ac:
            return "ISIK_AC", reply

    return "", reply


# ---- ENDPOINT'LER ----
@app.route("/stt", methods=["POST"])
def stt():
    pcm_data = request.get_data()
    if not pcm_data or len(pcm_data) < 100:
        return jsonify({"text": "", "reply": "", "command": "", "error": "Bos ses"}), 400

    sample_rate = int(request.headers.get("X-Sample-Rate", "16000"))

    wav_buf = io.BytesIO()
    with wave.open(wav_buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_data)

    try:
        r = httpx.post(
            "https://api.groq.com/openai/v1/audio/transcriptions",
            headers={"Authorization": f"Bearer {GROQ_API_KEY}"},
            files={"file": ("audio.wav", wav_buf.getvalue(), "audio/wav")},
            data={"model": "whisper-large-v3-turbo", "language": "tr"},
            timeout=15.0,
        )
        r.raise_for_status()
        text = r.json().get("text", "").strip()
    except Exception as e:
        print(f"[HATA] Groq Whisper: {e}")
        text = ""

    sicaklik = request.headers.get("X-Temperature", "")
    nem = request.headers.get("X-Humidity", "")

    sensor_bilgisi = ""
    if sicaklik and sicaklik != "-999.0":
        sensor_bilgisi = f" [Sensör: Sıcaklık={sicaklik}°C, Nem=%{nem}]"

    reply = ""
    if text:
        try:
            print(f"[SEN] {text}")
        except UnicodeEncodeError:
            print("[SEN] (encoding error)")

        # Sensörü sadece kullanıcı sıcaklık/nem sorduğunda modele ver (her cevapta söylemesin)
        ai_metin = text
        if sensor_bilgisi and kullanici_sicaklik_nem_soruyor_mu(text):
            ai_metin = text + sensor_bilgisi
            try:
                print(f"[SEN+SENSOR]{sensor_bilgisi}")
            except UnicodeEncodeError:
                pass

        reply = groq_yanit_al(ai_metin)

        try:
            print(f"[AI]  {reply}")
        except UnicodeEncodeError:
            print("[AI]  (encoding error)")

    command, reply = komut_algila(text, reply)
    reply = reply.replace("\\n", " ").strip()

    if command:
        print(f"[CMD] {command}")

    return jsonify({"text": text, "reply": reply, "command": command})


@app.route("/tts", methods=["POST"])
def tts():
    text = request.get_data(as_text=True).strip()
    if not text:
        return "Bos metin", 400

    try:
        print(f"[TTS] {text[:80]}...")
    except UnicodeEncodeError:
        print("[TTS] (encoding error)")

    try:
        pcm = metin_to_pcm(text)
    except Exception as e:
        print(f"[HATA] TTS: {e}")
        return "TTS hatasi", 500

    return Response(
        pcm,
        mimetype="application/octet-stream",
        headers={
            "X-Sample-Rate": str(TTS_SAMPLE_RATE),
            "X-Channels": "1",
            "X-Bits": "16",
        },
    )


# ---- BASLAT ----
if __name__ == "__main__":
    hostname = socket.gethostname()
    try:
        local_ip = socket.gethostbyname(hostname)
    except socket.gaierror:
        local_ip = "127.0.0.1"

    port = 5000
    print()
    print("=" * 55)
    print(f"  Sunucu: http://{local_ip}:{port}")
    print(f"  STT:  POST /stt   (ses -> metin + AI yanit)")
    print(f"  TTS:  POST /tts   (metin -> ses)")
    print(f"  Groq Whisper + Groq AI + Edge-TTS aktif")
    print("=" * 55)
    print()
    app.run(host="0.0.0.0", port=port, debug=False)
