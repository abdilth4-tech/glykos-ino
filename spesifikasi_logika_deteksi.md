# Spesifikasi Logika Deteksi — Smart Insole Diabetic Foot

Dokumen ini berisi spesifikasi lengkap logika deteksi untuk diimplementasikan ke
firmware ESP32 (Arduino/C++). Semua ambang batas diambil dari studi yang dikutip
di bagian akhir dokumen.

## Konteks Sistem

- 3x FSR402 (tekanan): HA (hallux), M1 (metatarsal 1), HE (heel/tumit)
- 2x NTC/KY-013 (suhu kulit): T1 (forefoot), T2 (heel)
- 1x SHT30 (kelembapan & suhu udara dalam sepatu)
- Semua sensor analog dibaca lewat voltage divider, dikonversi ke satuan fisik
  (kPa untuk FSR, °C untuk NTC) sebelum masuk ke logika deteksi di bawah.
- Siklus baca sensor: setiap 1-2 detik (sesuaikan dengan `SAMPLE_PERIOD` firmware).

---

## 1. Logika Tekanan (per titik FSR, dijalankan independen utk HA/M1/HE)

### 1a. Peringatan Puncak (immediate, tanpa syarat durasi)

```
KONSTANTA:
  P_WARN = 200.0   // kPa
  P_RISK = 250.0   // kPa

setiap sampel, untuk masing-masing FSR:
  jika P >= P_RISK:
    status_puncak = "RISIKO"
  jika sebaliknya P >= P_WARN:
    status_puncak = "PERHATIAN"
  jika sebaliknya:
    status_puncak = "AMAN"
```

### 1b. Peringatan Tekanan Bertahan (butuh state per sensor, berbasis durasi)

```
KONSTANTA:
  P_SUSTAIN_KPA     = 5.0        // ~35 mmHg, ambang tekanan statis
  SUSTAIN_DURATION_MS = 600000   // 10 menit

STATE per sensor (persisten antar sampel):
  waktu_mulai_tinggi : timestamp atau NULL
  alert_sustain       : boolean

setiap sampel, untuk masing-masing FSR:
  jika P >= P_SUSTAIN_KPA:
    jika waktu_mulai_tinggi == NULL:
      waktu_mulai_tinggi = waktu_sekarang
    jika (waktu_sekarang - waktu_mulai_tinggi) >= SUSTAIN_DURATION_MS:
      alert_sustain = TRUE
  sebaliknya (P < P_SUSTAIN_KPA):
    waktu_mulai_tinggi = NULL   // reset - kaki sudah "istirahat"/beban digeser
    alert_sustain = FALSE
```

**Catatan penting:** dua alert ini INDEPENDEN dan bisa aktif bersamaan atau
sendiri-sendiri. `status_puncak` menangkap tekanan tinggi sesaat (mis. saat
melangkah), `alert_sustain` menangkap tekanan rendah-sedang yang menetap tanpa
digeser (mis. saat berdiri/duduk lama tanpa sadar).

### 1c. Pressure-Time Integral / PTI (metrik akumulasi harian, bukan alert langsung)

```
STATE per sensor:
  pti_harian : float, akumulasi, direset tiap 24 jam (atau tiap ganti hari)

setiap sampel:
  delta_waktu_detik = waktu_sekarang - waktu_sampel_sebelumnya
  pti_harian += P * delta_waktu_detik   // satuan: kPa*detik

reset pti_harian ke 0 setiap pukul 00:00 (atau setiap kelipatan 24 jam sejak boot)
```
PTI dipakai untuk grafik tren/laporan (bukan untuk memicu alert real-time),
menunjukkan total "paparan tekanan" sepanjang hari.

---

## 2. Logika Suhu (per titik NTC, dijalankan independen utk T1/T2)

```
KONSTANTA:
  TEMP_DELTA_ALERT = 2.2   // derajat C, selisih vs baseline harian

STATE per sensor:
  baseline_suhu : float, ditangkap sekali saat kaki dalam kondisi
                  normal/istirahat (idealnya pagi hari sebelum aktivitas),
                  disimpan permanen (NVS/EEPROM), TIDAK direset otomatis

setiap sampel:
  delta = suhu_sekarang - baseline_suhu
  jika delta >= TEMP_DELTA_ALERT:
    alert_suhu = TRUE   // indikasi inflamasi lokal / pre-ulkus
  sebaliknya:
    alert_suhu = FALSE
```

**Cara menangkap baseline:** ambil rata-rata pembacaan (mis. 20 sampel dalam
2 detik) saat sistem baru dinyalakan dan kaki belum menahan beban/aktivitas.
Baseline BOLEH diperbarui manual lewat tombol/perintah "kalibrasi ulang", tapi
tidak boleh berubah otomatis tiap hari (supaya tren kenaikan bisa terdeteksi
across hari, bukan cuma dalam 1 sesi pakai).

---

## 3. Logika Kelembapan (satu sensor SHT30, satu status global)

```
KONSTANTA:
  RH_HIGH_WARN = 70.0   // %RH

setiap sampel:
  jika RH >= RH_HIGH_WARN:
    alert_humid = TRUE
  sebaliknya:
    alert_humid = FALSE
```

---

## 4. Status Gabungan (ringkasan level sistem, dikirim ke dashboard/JSON)

```
Untuk setiap titik FSR (HA, M1, HE), gabungkan jadi satu status akhir dengan
prioritas berikut (dari paling urgent ke paling ringan):

  1. status_puncak == "RISIKO"        -> "RISIKO_TINGGI"
  2. alert_sustain == TRUE            -> "TEKANAN_BERTAHAN"
  3. status_puncak == "PERHATIAN"     -> "PERHATIAN"
  4. sebaliknya                        -> "AMAN"

Untuk setiap titik NTC (T1, T2):
  alert_suhu == TRUE  -> "SUHU_NAIK"
  sebaliknya           -> "NORMAL"

Status kelembapan:
  alert_humid == TRUE -> "LEMBAP_TINGGI"
  sebaliknya           -> "NORMAL"

STATUS_SISTEM_KESELURUHAN = "RISIKO" jika ada MINIMAL SATU titik FSR yang
  berstatus "RISIKO_TINGGI" atau "TEKANAN_BERTAHAN", ATAU ada titik NTC yang
  "SUHU_NAIK". Jika tidak ada tapi ada "PERHATIAN"/"LEMBAP_TINGGI", status jadi
  "PERHATIAN". Selain itu, "AMAN".
```

---

## 5. Struktur Output/JSON yang Disarankan

```json
{
  "fsr": {
    "hallux":      {"kpa": 45.2, "status": "AMAN",   "sustain_alert": false, "pti_hari_ini": 128340},
    "metatarsal1": {"kpa": 212.0,"status": "PERHATIAN","sustain_alert": false, "pti_hari_ini": 540210},
    "heel":        {"kpa": 30.1, "status": "AMAN",   "sustain_alert": true,  "pti_hari_ini": 980010}
  },
  "ntc": {
    "forefoot": {"c": 31.2, "delta": 1.1, "alert": false},
    "heel":     {"c": 34.0, "delta": 2.6, "alert": true}
  },
  "humidity": {"rh": 74.2, "alert": true},
  "status_keseluruhan": "RISIKO"
}
```

---

## 6. Parameter yang Bisa Disesuaikan (expose sebagai konstanta/config, bukan hardcode dalam-dalam)

| Konstanta | Nilai default | Sumber |
|---|---|---|
| P_WARN | 200 kPa | Jones dkk. 2021 (systematic review in-shoe threshold) |
| P_RISK | 250 kPa | Konsensus umum ambang risiko ulkus |
| P_SUSTAIN_KPA | 5 kPa (~35 mmHg) | Jones dkk. 2021, ambang sustained pressure |
| SUSTAIN_DURATION_MS | 10 menit | Studi mekanoreseptor (efek iskemia kulit terukur pada durasi ini, khususnya di tumit) |
| TEMP_DELTA_ALERT | 2.2 °C | Studi termometri kaki diabetik (asimetri suhu prediktor pre-ulkus) |
| RH_HIGH_WARN | 70 %RH | Ambang umum risiko maserasi kulit/infeksi jamur |

---

## 7. Sumber/Dasar Riset

1. Jones, S. dkk. (2021). "In-Shoe Pressure Thresholds for People with Diabetes
   and Neuropathy at Risk of Ulceration: A Systematic Review." Journal of
   Diabetes and its Complications, 35, 107815. — sumber P_WARN=200kPa dan
   P_SUSTAIN=35mmHg.
2. Studi mekanoreseptor kulit telapak kaki (Frontiers in Neuroscience, 2024) —
   sumber durasi 10 menit sebagai titik waktu munculnya efek iskemia terukur
   akibat tekanan berkelanjutan, terutama di tumit.
3. Studi kasus-kontrol tekanan plantar pada ulkus diabetik (2016, PMC5024422) —
   sumber konsep Pressure-Time Integral sebagai metrik yang signifikan
   berkorelasi dengan kejadian ulkus.
4. Morales-Morales dkk. (2026), Technologies 14(6):362 — dasar desain sistem
   smart insole secara umum (ESP32+FSR402+LiPo, posisi sensor).

---

## 8. Catatan Implementasi untuk Claude Code

- Implementasikan state (waktu_mulai_tinggi, alert_sustain, pti_harian,
  baseline_suhu) sebagai variabel global per-sensor di firmware ESP32 (bukan
  lokal ke fungsi), karena harus persisten antar pemanggilan loop().
- `baseline_suhu` sebaiknya disimpan ke NVS (Preferences.h) supaya tidak hilang
  saat reboot.
- `pti_harian` boleh disimpan di RAM saja (reset tiap reboot cukup, karena ini
  metrik harian, bukan riwayat jangka panjang) KECUALI ada rencana histori
  jangka panjang tersimpan di server/SD card.
- Jalankan logika ini di dalam fungsi terpisah, mis. `evaluasiAlert()`, yang
  dipanggil setiap siklus baca sensor, supaya mudah diuji terpisah dari kode
  pembacaan ADC/I2C.
