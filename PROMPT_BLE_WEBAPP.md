# Prompt Claude CLI — Terima Data BLE ESP32 di Web App

Dokumen ini berisi **prompt siap-tempel** untuk Claude CLI di project web app (`index.html`)
supaya bisa menerima data dari ESP32 Smart Insole lewat BLE, plus **kontrak BLE** firmware
sebagai acuan.

---

## 1. Prompt siap-tempel

Salin blok di bawah ini ke Claude CLI di project web app Anda:

```
Pastikan web app (index.html) bisa menerima dan menampilkan data BLE dari
ESP32 firmware Smart Insole. Gunakan Web Bluetooth API. Spesifikasi BLE dari
firmware (jangan diubah, harus cocok persis):

- Perangkat BLE bernama: "glykos device"
- Nordic UART Service (NUS):
    Service UUID  : 6e400001-b5a3-f393-e0a9-e50e24dcca9e
    Characteristic TX (NOTIFY, ESP -> HP): 6e400003-b5a3-f393-e0a9-e50e24dcca9e
- Koneksi difilter berdasarkan Service UUID (bukan nama).
- Data dikirim sebagai notify, format CSV per paket, diakhiri "\n", contoh:
    F1:1234,F2:1180,F3:1502,P1:120.0,P2:80.0,P3:150.0,T1:29.4,T2:30.1,RH:55.2,TA:28.0,AX:0.01,AY:0.02,AZ:0.98
  Arti key:
    F1/F2/F3 = tegangan mentah FSR (mV)
    P1/P2/P3 = tekanan siap pakai (kPa) untuk Hallux/Metatarsal1/Tumit
    T1/T2    = suhu NTC forefoot/tumit (Celsius)
    RH       = kelembapan (%),  TA = suhu udara (Celsius)
    AX/AY/AZ = akselerasi MPU6050 (g)
  Catatan: sebagian key bisa TIDAK ADA di satu paket kalau sensornya tidak
  terdeteksi (mis. tanpa RH/TA/AX/AY/AZ). Parser harus tahan terhadap key
  yang hilang dan terhadap paket yang terpotong sebagian (jangan crash,
  pakai nilai terakhir yang valid).

Yang harus dipastikan ada / berfungsi:
1. Tombol connect yang memanggil navigator.bluetooth.requestDevice dengan
   filter Service UUID di atas, lalu startNotifications di karakteristik TX.
2. Listener characteristicvaluechanged yang men-decode TextDecoder, split
   per baris ("\n") lalu per "," lalu "key:value", dan menaruh ke UI.
3. Prioritaskan P1/P2/P3 (kPa siap pakai) jika ada; fallback hitung dari
   F1/F2/F3 hanya kalau P tidak dikirim.
4. Tampilkan status koneksi (tersambung / terputus / gagal) dan tangani event
   gattserverdisconnected.
5. Ingatkan syarat Web Bluetooth: hanya jalan di Chrome/Edge dan lewat
   http://localhost atau HTTPS (bukan file://).

Cek dulu apakah index.html SUDAH punya implementasi ini. Kalau sudah, jangan
ditulis ulang -- cukup verifikasi kecocokan UUID/format dan perbaiki bagian
yang salah saja. Jangan ubah UUID atau nama key CSV.
```

---

## 2. Kontrak BLE firmware (acuan)

| Item | Nilai |
|------|-------|
| Nama perangkat | `glykos device` |
| Service UUID | `6e400001-b5a3-f393-e0a9-e50e24dcca9e` |
| Characteristic TX (NOTIFY, ESP → HP) | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` |
| Filter koneksi | berdasarkan **Service UUID** (bukan nama) |
| Format | CSV `key:value` dipisah `,` diakhiri `\n` |
| MTU | firmware minta 200 (`BLEDevice::setMTU(200)`) agar paket panjang tidak terpotong |

### Arti key CSV

| Key | Arti | Satuan |
|-----|------|--------|
| `F1` `F2` `F3` | Tegangan mentah FSR (Hallux / Metatarsal1 / Tumit) | mV |
| `P1` `P2` `P3` | Tekanan siap pakai (Hallux / Metatarsal1 / Tumit) | kPa |
| `T1` `T2` | Suhu NTC (forefoot / tumit) | °C |
| `RH` | Kelembapan udara | % |
| `TA` | Suhu udara | °C |
| `AX` `AY` `AZ` | Akselerasi MPU6050 | g |

> **Catatan:** key `RH`, `TA`, `AX`, `AY`, `AZ` **hanya dikirim jika sensornya terdeteksi**.
> Parser web app harus tahan terhadap key yang tidak ada.

---

## 3. Syarat menjalankan (Web Bluetooth)

- Browser: **Chrome / Edge** (desktop atau Android). Safari / iOS tidak didukung.
- Halaman harus dibuka lewat **`http://localhost`** atau **HTTPS** — **bukan** `file://`.
- Cara cepat serve lokal (jalankan di folder web app):
  ```
  python -m http.server 8000
  ```
  lalu buka `http://localhost:8000` di Chrome.

---

## 4. Status saat dokumen ini dibuat

- `index.html` **sudah punya** implementasi Web Bluetooth yang cocok dengan firmware
  (UUID sama; sudah pakai P1/P2/P3 dengan fallback F1/F2/F3; sudah menangani disconnect).
- Jadi untuk sekadar **menjalankan**, prompt di atas tidak wajib — cukup buka via
  `http://localhost`, klik "Sambungkan via Bluetooth", pilih **"glykos device"**.
- Prompt di atas berguna untuk **memverifikasi / merapikan** kode penerima, atau
  membangunnya dari nol di project baru.
- Catatan kecil: teks fallback status di `index.html` masih tertulis "Smart Insole BLE"
  (kosmetik; koneksi tetap jalan karena pakai UUID, bukan nama).
