# Glykos — Smart Insole untuk Diabetic Foot

Sistem *smart insole* (alas kaki pintar) untuk deteksi dini risiko ulkus pada kaki diabetik.
Firmware **ESP32** membaca sensor tekanan, suhu, dan kelembapan dari dalam sepatu, lalu
mengirimkannya lewat **BLE (Bluetooth Low Energy)** ke **web app** untuk divisualisasikan
sebagai peta tekanan kaki secara real-time.

---

## Fitur

- **Pemantauan tekanan plantar** di 3 titik (Hallux, Metatarsal 1, Tumit) via FSR402.
- **Pemantauan suhu kulit** di 2 titik (forefoot & tumit) via NTC — deteksi inflamasi/pre-ulkus.
- **Pemantauan kelembapan & suhu udara** dalam sepatu via SHT30.
- **Deteksi gerak/langkah** via MPU6050 (akselerometer).
- **Streaming data via BLE** (Nordic UART Service) — tanpa kabel, bisa dibaca HP/laptop.
- **Web app** dengan Web Bluetooth API: peta kaki, kalibrasi FSR, dan mode timbangan.
- **Logika deteksi berbasis riset** (ambang tekanan, delta suhu, PTI) — lihat spesifikasi.

---

## Struktur Repo

| File / Folder | Keterangan |
|---|---|
| `test_full_semua_sensor.ino` | Firmware ESP32: baca semua sensor + kirim via Serial & BLE |
| `test_full_semua_sensor/` | Salinan sketch untuk struktur folder Arduino |
| `index.html` | Web app utama — peta tekanan kaki, terima data BLE |
| `kalibrasi.html` | Halaman kalibrasi sensor FSR |
| `timbangan_fsr.html` | Mode timbangan (uji pembacaan FSR sebagai berat) |
| `spesifikasi_logika_deteksi.md` | Spesifikasi lengkap logika deteksi (ambang batas + sumber riset) |
| `PROMPT_BLE_WEBAPP.md` | Kontrak BLE firmware ↔ web app + prompt bantu |

---

## Perangkat Keras

| Sensor | Fungsi | Koneksi |
|---|---|---|
| 3× FSR402 | Tekanan plantar (Hallux / Metatarsal 1 / Tumit) | Analog (voltage divider) |
| 2× NTC / KY-013 | Suhu kulit (forefoot / tumit) | Analog (voltage divider) |
| 1× SHT30 | Kelembapan & suhu udara | I2C (SDA=D21, SCL=D22) |
| 1× MPU6050 | Akselerometer / deteksi langkah | I2C (SDA=D21, SCL=D22) |
| ESP32 | Mikrokontroler + BLE | — |

---

## Kontrak BLE

| Item | Nilai |
|---|---|
| Nama perangkat | `glykos device` |
| Service UUID (NUS) | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| Characteristic TX (NOTIFY, ESP → HP) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| Filter koneksi | berdasarkan **Service UUID** (bukan nama) |
| Format data | CSV `key:value` dipisah `,`, diakhiri `\n` |
| MTU | firmware minta 200 byte agar paket panjang tidak terpotong |

Contoh paket:

```
F1:1234,F2:1180,F3:1502,P1:120.0,P2:80.0,P3:150.0,T1:29.4,T2:30.1,RH:55.2,TA:28.0,AX:0.01,AY:0.02,AZ:0.98
```

| Key | Arti | Satuan |
|---|---|---|
| `F1` `F2` `F3` | Tegangan mentah FSR (Hallux / Metatarsal1 / Tumit) | mV |
| `P1` `P2` `P3` | Tekanan siap pakai (Hallux / Metatarsal1 / Tumit) | kPa |
| `T1` `T2` | Suhu NTC (forefoot / tumit) | °C |
| `RH` | Kelembapan udara | % |
| `TA` | Suhu udara | °C |
| `AX` `AY` `AZ` | Akselerasi MPU6050 | g |

> Key `RH`, `TA`, `AX`, `AY`, `AZ` hanya dikirim jika sensornya terdeteksi. Parser harus tahan terhadap key yang hilang.

Detail lengkap ada di [`PROMPT_BLE_WEBAPP.md`](PROMPT_BLE_WEBAPP.md).

---

## Cara Menjalankan

### 1. Upload Firmware ke ESP32

Install library berikut lewat Arduino Library Manager:

- **Adafruit SHT31 Library** (kompatibel untuk SHT30)
- **Adafruit Unified Sensor** (dependency)

Library BLE (`BLEDevice.h` dkk.) sudah bawaan core ESP32.

Buka `test_full_semua_sensor.ino`, pilih board ESP32, lalu upload.
Cek Serial Monitor (baud **115200**) untuk melihat data lewat kabel.

### 2. Jalankan Web App

Web Bluetooth **hanya jalan di Chrome/Edge** (desktop atau Android) dan lewat
`http://localhost` atau HTTPS — **bukan** `file://`.

Serve folder ini secara lokal:

```bash
python -m http.server 8000
```

Lalu buka `http://localhost:8000/index.html` di Chrome, klik **Sambungkan via Bluetooth**,
dan pilih perangkat **"glykos device"**.

---

## Logika Deteksi (ringkas)

| Parameter | Default | Keterangan |
|---|---|---|
| `P_WARN` | 200 kPa | Ambang perhatian tekanan puncak |
| `P_RISK` | 250 kPa | Ambang risiko ulkus |
| `P_SUSTAIN_KPA` | 5 kPa (~35 mmHg) | Ambang tekanan statis bertahan |
| `SUSTAIN_DURATION_MS` | 10 menit | Durasi tekanan menetap sebelum alert |
| `TEMP_DELTA_ALERT` | 2.2 °C | Selisih suhu vs baseline → indikasi inflamasi |
| `RH_HIGH_WARN` | 70 %RH | Ambang risiko maserasi kulit |

Spesifikasi lengkap (termasuk PTI, status gabungan, dan sumber riset) ada di
[`spesifikasi_logika_deteksi.md`](spesifikasi_logika_deteksi.md).

---

## Catatan

- Proyek ini bersifat edukatif/riset, **bukan alat diagnostik medis**. Konsultasikan
  keputusan klinis dengan tenaga kesehatan.
- Ambang batas deteksi diambil dari studi yang dikutip di spesifikasi logika deteksi.
