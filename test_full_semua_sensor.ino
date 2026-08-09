/*
  ============================================================================
  TES 3 FSR + 2 NTC + SHT30 + MPU6050 - Smart Insole (Serial Monitor + BLE)
  ----------------------------------------------------------------------------
  Data sensor ditampilkan lewat Serial Monitor (kabel USB, baud 115200) DAN
  dikirim lewat BLE (notify) supaya bisa dibaca HP tanpa kabel.

    - SHT30 (kelembapan & suhu udara)   -> I2C, pin SDA=D21, SCL=D22
    - MPU6050 (deteksi langkah)         -> I2C, pin SDA=D21, SCL=D22 (bareng SHT30)
    - BLE (Bluetooth Low Energy)        -> pakai UUID Nordic UART Service (NUS),
                                            jadi bisa langsung dites pakai app
                                            "Serial Bluetooth Terminal" (Android)
                                            tanpa perlu bikin app sendiri dulu.

  Library tambahan yang WAJIB diinstall dulu lewat Library Manager:
    - "Adafruit SHT31 Library"   (dipakai juga untuk SHT30, kompatibel)
    - "Adafruit Unified Sensor"  (dependency dari library di atas)
    - Library BLE (BLEDevice.h dkk) sudah bawaan core ESP32, tidak perlu install.

  Cara pakai:
    1. Upload sketch ini, buka Serial Monitor (baud 115200) kalau mau lihat
       lewat kabel juga.
    2. Di HP, scan BLE cari nama perangkat "Smart Insole BLE", connect.
    3. Kalau pakai app "Serial Bluetooth Terminal": pilih mode BLE, connect ke
       perangkatnya, aktifkan notify di karakteristik TX -> data sensor akan
       muncul terus tiap ~300ms dalam format CSV singkat, contoh:
       F1:1234,F2:1180,F3:1502,T1:29.4,T2:30.1,RH:55.2,TA:28.0,AX:0.01,AY:0.02,AZ:0.98
  ============================================================================
*/

#include <Wire.h>
#include <Adafruit_SHT31.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Ticker.h>

Adafruit_SHT31 sht = Adafruit_SHT31();
bool shtOK = false;

// ---- BLE (pakai UUID Nordic UART Service supaya kompatibel dgn app umum) ----
const char* BLE_NAMA           = "glykos device";
#define BLE_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHARACTERISTIC_TX   "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* bleServer = NULL;
BLECharacteristic* bleTx = NULL;
bool bleTerhubung = false;
volatile bool bleBaruPutus = false; // di-set saat putus; restart advertising ditangani di loop()

class BleServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* s) { bleTerhubung = true; }
  void onDisconnect(BLEServer* s) {
    bleTerhubung = false;
    bleBaruPutus = true; // JANGAN startAdvertising() di dalam callback (stack belum bersih)
  }
};

void bleInit() {
  BLEDevice::init(BLE_NAMA);
  BLEDevice::setMTU(200); // minta MTU besar supaya paket CSV panjang tidak terpotong di 20 byte
  bleServer = BLEDevice::createServer();
  bleServer->setCallbacks(new BleServerCallbacks());

  BLEService* service = bleServer->createService(BLE_SERVICE_UUID);
  bleTx = service->createCharacteristic(BLE_CHARACTERISTIC_TX, BLECharacteristic::PROPERTY_NOTIFY);
  bleTx->addDescriptor(new BLE2902());
  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->start();
}

void bleKirim(const String &data) {
  if (!bleTerhubung) return;
  String paket = data + "\n"; // terminator supaya app terminal/parser bisa pisahkan antar paket
  bleTx->setValue(paket.c_str());
  bleTx->notify();
}

// ---- LED indikator status BLE ----
// GPIO2 = LED bawaan di sebagian besar board ESP32 DevKit (tanpa wiring tambahan).
// LED eksternal: GPIO2 -> resistor 220-330 ohm -> kaki panjang LED (+),
//                kaki pendek LED (-) -> GND.
const int PIN_LED = 2;
Ticker ledTicker;

void ledToggle() { digitalWrite(PIN_LED, !digitalRead(PIN_LED)); }

// Terapkan pola LED HANYA saat status koneksi berubah (biar Ticker tidak
// di-restart tiap loop). Ticker jalan sendiri di background, jadi kedipnya tetap
// cepat walau loop ada delay(300).
void ledUpdate() {
  static int terakhir = -1;
  int sekarang = bleTerhubung ? 1 : 0;
  if (sekarang == terakhir) return;
  terakhir = sekarang;
  if (bleTerhubung) {
    ledTicker.detach();
    digitalWrite(PIN_LED, HIGH);          // menyala solid = BLE tersambung
  } else {
    ledTicker.attach_ms(100, ledToggle);  // kedip tiap 100ms (~5x/detik) = belum tersambung
  }
}

// ---- Pin FSR (samakan dengan label FISIK di board ESP32-mu) ----
const int PIN_FSR1 = 36; // tertulis "VP" di board
const int PIN_FSR2 = 39; // tertulis "VN" di board
const int PIN_FSR3 = 34; // tertulis "D34" di board

const char* NAMA_FSR1 = "FSR1-Hallux";
const char* NAMA_FSR2 = "FSR2-Metatarsal1";
const char* NAMA_FSR3 = "FSR3-Tumit";

// ---- Pin NTC (modul KY-013, pin S ke sini) ----
const int PIN_NTC1 = 35; // tertulis "D35" di board -> NTC 1 (forefoot)
const int PIN_NTC2 = 32; // tertulis "D32" di board -> NTC 2 (tumit)

const char* NAMA_NTC1 = "NTC1-Forefoot";
const char* NAMA_NTC2 = "NTC2-Tumit";

// ---- Konstanta konversi KY-013 (nilai standar pabrik) ----
const float VCC_MV   = 3300.0; // tegangan suplai modul (3V3 dari ESP32)
const float NTC_R0   = 10000.0;  // resistansi NTC @ 25C (standar KY-013)
const float NTC_RF   = 10000.0;  // resistor tetap di dalam modul (standar KY-013)
const float NTC_T0K  = 298.15;   // 25C dalam Kelvin
const float NTC_BETA = 3950.0;   // nilai Beta standar KY-013

const int ADC_SAMPLES = 16; // rata-rata beberapa sampel biar stabil (dinaikkan dari 8)

// ---- Kalibrasi FSR (hasil kalibrasi.html, 31/7/2026 16:31) ----
// Rfsr = FSR_RM * (VCC/Vnode - 1) ; G(uS) = 1e6/Rfsr ; F(N) = FSR_A[i]*G + FSR_B[i]
// Urutan array: 0=Hallux, 1=Metatarsal1, 2=Tumit (sama dgn PIN_FSR1/2/3)
const float FSR_RM       = 10000.0;
float       FSR_A[3]     = {0.006052, 0.012290, 0.005939}; // N/uS
float       FSR_B[3]     = {1.642072, 0.097899, 2.557779}; // N
const float FSR_AREA_CM2 = 1.69;

// EMA (exponential moving average) per sensor - redam jitter kPa akibat noise
// ADC/kontak FSR supaya angka "diam" saat beban benar-benar diam.
const float EMA_ALPHA_FSR = 0.25; // makin kecil = makin halus tapi makin lambat
float emaKpaFsr[3] = {NAN, NAN, NAN};

float fsrResistance(float vnode_mv) {
  if (vnode_mv < 1.0)     return INFINITY; // ~tanpa beban
  if (vnode_mv >= VCC_MV) return 0.0;      // saturasi
  return FSR_RM * (VCC_MV / vnode_mv - 1.0);
}

float fsrPressureKPa(float rfsr, int idx) {
  if (!isfinite(rfsr) || rfsr <= 0) return 0.0;
  float G = 1e6 / rfsr;
  float F = FSR_A[idx] * G + FSR_B[idx];
  if (F < 0) F = 0;
  float area_m2 = FSR_AREA_CM2 * 1e-4;
  return (F / area_m2) / 1000.0;
}

float haluskanKpa(float nilaiBaru, int idx) {
  emaKpaFsr[idx] = isnan(emaKpaFsr[idx]) ? nilaiBaru : (EMA_ALPHA_FSR * nilaiBaru + (1 - EMA_ALPHA_FSR) * emaKpaFsr[idx]);
  return emaKpaFsr[idx];
}

// ---- MPU6050 (koordinat akselerasi) ----
const uint8_t MPU_ADDR = 0x68;

void mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00); // wake up MPU6050
  Wire.endTransmission(true);
}

void i2cScan() {
  Serial.println("Scanning I2C...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Alamat I2C ditemukan: 0x%02X\n", addr);
      found++;
    }
  }
  if (found == 0) Serial.println("  Tidak ada perangkat I2C yang terdeteksi sama sekali!");
  Serial.println("Selesai scan I2C.");
}

bool mpuReadG(float &gx, float &gy, float &gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false; // MPU6050 tidak terdeteksi
  Wire.requestFrom((int)MPU_ADDR, 6, true);
  if (Wire.available() < 6) return false;
  int16_t ax = (Wire.read() << 8) | Wire.read();
  int16_t ay = (Wire.read() << 8) | Wire.read();
  int16_t az = (Wire.read() << 8) | Wire.read();
  gx = ax / 16384.0; gy = ay / 16384.0; gz = az / 16384.0;
  return true;
}

float bacaMv(int pin) {
  uint32_t total = 0;
  for (int i = 0; i < ADC_SAMPLES; i++) {
    total += analogReadMilliVolts(pin); // pembacaan terkalibrasi (bukan analogRead biasa)
  }
  return (float)total / ADC_SAMPLES;
}

// Modul KY-013: S -> node antara NTC & resistor internal, sama prinsipnya
// dengan divider FSR: Rntc = RF * (VCC/Vnode - 1)
float ntcTempC(float vnode_mv) {
  if (vnode_mv < 1.0 || vnode_mv >= VCC_MV) return NAN; // wiring bermasalah
  float rntc = NTC_RF * (VCC_MV / vnode_mv - 1.0);
  float invT = 1.0/NTC_T0K + (1.0/NTC_BETA) * log(rntc / NTC_R0);
  return (1.0/invT) - 273.15;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db); // supaya bisa baca 0-3.3V penuh

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // ---- I2C: SHT30 & MPU6050 (SDA=D21, SCL=D22) ----
  Wire.begin(21, 22);
  i2cScan();
  shtOK = sht.begin(0x44);
  if (!shtOK) Serial.println("SHT30 tidak terdeteksi! Cek wiring SDA/SCL/3V3/GND.");
  mpuInit();

  bleInit();
  ledUpdate(); // mulai dengan kedip cepat (belum ada yang connect)

  Serial.println();
  Serial.println("==================================================");
  Serial.println("TES 3 FSR + 2 NTC + SHT30 + MPU6050 - Smart Insole");
  Serial.print("BLE aktif, nama perangkat: "); Serial.println(BLE_NAMA);
  Serial.println("Diamkan dulu (tanpa tekanan/sentuhan) beberapa detik.");
  Serial.println("Lalu coba tekan tiap FSR & pegang tiap NTC satu-satu.");
  Serial.println("==================================================");
  Serial.println();
}

void loop() {
  // Restart advertising di sini (bukan di dalam callback onDisconnect) supaya
  // stack BLE sempat membersihkan koneksi lama -> reconnect lebih andal.
  if (bleBaruPutus) {
    delay(500);
    BLEDevice::startAdvertising();
    bleBaruPutus = false;
  }

  ledUpdate(); // sesuaikan LED: solid kalau tersambung, kedip cepat kalau belum

  // ---- FSR ----
  float v1 = bacaMv(PIN_FSR1);
  float v2 = bacaMv(PIN_FSR2);
  float v3 = bacaMv(PIN_FSR3);
  float p1 = haluskanKpa(fsrPressureKPa(fsrResistance(v1), 0), 0);
  float p2 = haluskanKpa(fsrPressureKPa(fsrResistance(v2), 1), 1);
  float p3 = haluskanKpa(fsrPressureKPa(fsrResistance(v3), 2), 2);

  Serial.printf("%-18s %6.0f mV %6.1f kPa | %-18s %6.0f mV %6.1f kPa | %-18s %6.0f mV %6.1f kPa\n",
                NAMA_FSR1, v1, p1, NAMA_FSR2, v2, p2, NAMA_FSR3, v3, p3);

  // ---- NTC ----
  float vn1 = bacaMv(PIN_NTC1);
  float vn2 = bacaMv(PIN_NTC2);
  float t1  = ntcTempC(vn1);
  float t2  = ntcTempC(vn2);

  Serial.printf("%-18s %6.2f C     |   %-18s %6.2f C\n",
                NAMA_NTC1, t1, NAMA_NTC2, t2);

  // ---- SHT30 (baca SEKALI per loop, dipakai bareng di Serial & BLE) ----
  float rh = NAN, ta = NAN;
  if (shtOK) {
    rh = sht.readHumidity();
    ta = sht.readTemperature();
    Serial.printf("Kelembapan: %5.1f %%RH  |  Suhu udara: %5.1f C\n", rh, ta);
  } else {
    Serial.println("SHT30 tidak terdeteksi");
  }

  // ---- MPU6050 (koordinat) ----
  float gx, gy, gz;
  bool mpuOK = mpuReadG(gx, gy, gz);
  if (mpuOK) {
    Serial.printf("MPU6050  X: %6.2f g  |  Y: %6.2f g  |  Z: %6.2f g\n", gx, gy, gz);
  } else {
    Serial.println("MPU6050 tidak terdeteksi");
  }
  Serial.println("--------------------------------------------------------------------------------");

  // ---- Kirim semua data lewat BLE (format CSV singkat) ----
  String dataBle = "F1:" + String(v1,0) + ",F2:" + String(v2,0) + ",F3:" + String(v3,0) +
                    ",P1:" + String(p1,1) + ",P2:" + String(p2,1) + ",P3:" + String(p3,1) +
                    ",T1:" + String(t1,1) + ",T2:" + String(t2,1);
  if (shtOK) {
    dataBle += ",RH:" + String(rh,1) + ",TA:" + String(ta,1);
  }
  if (mpuOK) {
    dataBle += ",AX:" + String(gx,2) + ",AY:" + String(gy,2) + ",AZ:" + String(gz,2);
  }
  bleKirim(dataBle);

  delay(300); // update ~3x per detik, cukup buat lihat perubahan tekanan/suhu
}
