#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <PZEM004Tv30.h>

// Inisialisasi Jaringan
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };   // MAC address
IPAddress ipArduino(192, 168, 1, 177);                 // IP statis
IPAddress ipServer(192, 168, 1, 10);                   // IP Server
const int portServer = 80;                             // Port web server (biasanya 80)
String endpoint = "/api/simpan-data";                  // Route API di Laravel

// Inisialisasi LCD
const int LCD_COLS = 20;
const int LCD_ROWS = 4;
LiquidCrystal_I2C lcd(0x27, LCD_COLS, LCD_ROWS);

//Tombol
const int pinTombol1 = 9; // pin 9
const int pinTombol2 = 8; // pin 8
const int pinTombol3 = 7; // pin 7
const int pinTombol4 = 6; // pin 6

//Interval Pengiriman
const long intervalKirimData = 10000;  // Kirim data ke server setiap 10 detik
const long intervalBacaSensor = 2000; // Baca sensor setiap 2 detik

//Inisalisasi Sensor
PZEM004Tv30 pzem_r(&Serial1);
PZEM004Tv30 pzem_s(&Serial2);
PZEM004Tv30 pzem_t(&Serial3);

//Inisialisasi Ethernet
EthernetClient client;

// Variable simpan data sensor
float vr, ir, pr, er, fr, pfr;
float vs, is, ps, es, fs, pfs;
float vt, it, pt, et, ft, pft;
float v_rs, v_st, v_tr;

// Variabel untuk manajemen waktu non-blocking
unsigned long waktuKirimSebelumnya = 0;
unsigned long waktuBacaSebelumnya = 0;

// Variabel untuk manajemen tampilan LCD (State Machine)
enum Tampilan { TAMPILAN_R, TAMPILAN_S, TAMPILAN_T, TAMPILAN_OVERVIEW, TAMPILAN_DEBUG, MENU_RESET_CONFIRM };
Tampilan layarAktif = TAMPILAN_R;

// Variabel untuk status sistem
bool modeOffline = false;
String statusKirimTerakhir = "Menunggu";

void setup() {
  Serial.begin(9600);
  
  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Sistem Monitoring");
  lcd.setCursor(0, 1);
  lcd.print("3 Fasa - Skripsi");
  delay(2000);

  // Inisialisasi Tombol
  pinMode(pinTombol1, INPUT_PULLUP);
  pinMode(pinTombol2, INPUT_PULLUP);
  pinMode(pinTombol3, INPUT_PULLUP);
  pinMode(pinTombol4, INPUT_PULLUP);

  // Inisialisasi Ethernet
  lcd.clear();
  lcd.print("Mencari Alamat IP...");
  if (Ethernet.begin(mac) == 0) {
    // Gagal mendapatkan IP dari DHCP (jika tidak pakai IP statis)
    Serial.println("Gagal dapat IP dari DHCP.");
    lcd.clear();
    lcd.print("DHCP Gagal. Pakai Statis!");
    // Coba lagi dengan IP statis
    delay(1000);
    Ethernet.begin(mac, ipArduino);
  }

  delay(1000); // Beri waktu untuk koneksi

  // Logika penanganan error koneksi
  if (Ethernet.linkStatus() == LinkOFF || Ethernet.localIP() == IPAddress(0,0,0,0) ) {
    modeOffline = handleConnectionError();
  } else {
    lcd.clear();
    lcd.print("Terhubung!");
    lcd.setCursor(0, 1);
    lcd.print("IP: ");
    lcd.print(Ethernet.localIP());
    delay(2000);
  }
}

void loop() {
  // 1. Cek input keypad
  handleKeypad();

  // 2. Baca data sensor secara berkala
  if (millis() - waktuBacaSebelumnya >= intervalBacaSensor) {
    waktuBacaSebelumnya = millis();
    bacaSemuaSensor();
    updateLcdDisplay(); // Perbarui tampilan LCD setelah baca sensor
  }

  // 3. Kirim data ke server secara berkala (jika tidak mode offline)
  if (!modeOffline && (millis() - waktuKirimSebelumnya >= intervalKirimData)) {
    waktuKirimSebelumnya = millis();
    kirimDataKeServer();
  }
}

void handleKeypad() {
  char key = 0; // Variabel untuk menyimpan tombol yang ditekan

  // Baca setiap pin tombol. Jika LOW, berarti tombol itu sedang ditekan.
  if (digitalRead(pinTombol1) == LOW) key = '1';
  else if (digitalRead(pinTombol2) == LOW) key = '2';
  else if (digitalRead(pinTombol3) == LOW) key = '3';
  else if (digitalRead(pinTombol4) == LOW) key = '4';

  // Jika ada tombol yang terdeteksi (key tidak lagi 0)
  if (key) {
    // Logika navigasi dan menu (SAMA SEPERTI SEBELUMNYA)
    if (layarAktif == MENU_RESET_CONFIRM) {
      if (key == '1') { layarAktif = TAMPILAN_R; } 
      else if (key == '2') {
        lcd.clear();
        lcd.print("Resetting Energi...");
        pzem_r.resetEnergy();
        pzem_s.resetEnergy();
        pzem_t.resetEnergy();
        delay(1000);
        layarAktif = TAMPILAN_R;
      }
    } else {
      switch (key) {
        case '1':
          if (layarAktif == TAMPILAN_R) layarAktif = TAMPILAN_T;
          else layarAktif = (Tampilan)(layarAktif - 1);
          break;
        case '2':
          if (layarAktif == TAMPILAN_T) layarAktif = TAMPILAN_R;
          else layarAktif = (Tampilan)(layarAktif + 1);
          break;
        case '3':
          layarAktif = MENU_RESET_CONFIRM;
          break;
        case '4':
          if (layarAktif == TAMPILAN_OVERVIEW) layarAktif = TAMPILAN_DEBUG;
          else if (layarAktif == TAMPILAN_DEBUG) layarAktif = TAMPILAN_R;
          else layarAktif = TAMPILAN_OVERVIEW;
          break;
      }
    }
    updateLcdDisplay(); // Perbarui tampilan setelah tombol ditekan
    delay(200); 
  }
}

void updateLcdDisplay() {
  lcd.clear();
  switch (layarAktif) {
    case TAMPILAN_R:
      lcd.print("FASA R     V/A/W");
      lcd.setCursor(0, 1); lcd.print("V: "); lcd.print(vr, 2);
      lcd.setCursor(0, 2); lcd.print("A: "); lcd.print(ir, 2);
      lcd.setCursor(0, 3); lcd.print("W: "); lcd.print(pr, 2);
      lcd.setCursor(12, 1); lcd.print("kWh:"); lcd.setCursor(12, 2); lcd.print(er, 2);
      lcd.setCursor(12, 3); lcd.print("PF :"); lcd.setCursor(12, 4); lcd.print(pfr, 2);
      break;
    case TAMPILAN_S:
      lcd.print("FASA S     V/A/W");
      lcd.setCursor(0, 1); lcd.print("V: "); lcd.print(vs, 2);
      lcd.setCursor(0, 2); lcd.print("A: "); lcd.print(is, 2);
      lcd.setCursor(0, 3); lcd.print("W: "); lcd.print(ps, 2);
      lcd.setCursor(12, 1); lcd.print("kWh:"); lcd.setCursor(12, 2); lcd.print(es, 2);
      lcd.setCursor(12, 3); lcd.print("PF :"); lcd.setCursor(12, 4); lcd.print(pfs, 2);
      break;
    case TAMPILAN_T:
      lcd.print("FASA T     V/A/W");
      lcd.setCursor(0, 1); lcd.print("V: "); lcd.print(vt, 2);
      lcd.setCursor(0, 2); lcd.print("A: "); lcd.print(it, 2);
      lcd.setCursor(0, 3); lcd.print("W: "); lcd.print(pt, 2);
      lcd.setCursor(12, 1); lcd.print("kWh:"); lcd.setCursor(12, 2); lcd.print(et, 2);
      lcd.setCursor(12, 3); lcd.print("PF :"); lcd.setCursor(12, 4); lcd.print(pft, 2);
      break;
    case TAMPILAN_OVERVIEW:
      lcd.print("OVERVIEW DAYA (WATT)");
      lcd.setCursor(0, 1); lcd.print("R: "); lcd.print(pr, 2);
      lcd.setCursor(0, 2); lcd.print("S: "); lcd.print(ps, 2);
      lcd.setCursor(0, 3); lcd.print("T: "); lcd.print(pt, 2);
      break;
    case TAMPILAN_DEBUG:
      lcd.print("STATUS DEBUG");
      lcd.setCursor(0, 1); lcd.print("IP: "); lcd.print(Ethernet.localIP());
      lcd.setCursor(0, 2); lcd.print("Mode: "); lcd.print(modeOffline ? "Offline" : "Online");
      lcd.setCursor(0, 3); lcd.print("Kirim: "); lcd.print(statusKirimTerakhir);
      break;
    case MENU_RESET_CONFIRM:
      lcd.print("Reset Energi (kWh)?");
      lcd.setCursor(0, 2); lcd.print("1: Tidak");
      lcd.setCursor(10, 2); lcd.print("2: Ya");
      break;
  }
}

void bacaSemuaSensor() {
  // Baca semua nilai dari sensor
  vr = pzem_r.voltage(); ir = pzem_r.current(); pr = pzem_r.power(); er = pzem_r.energy(); fr = pzem_r.frequency(); pfr = pzem_r.pf();
  vs = pzem_s.voltage(); is = pzem_s.current(); ps = pzem_s.power(); es = pzem_s.energy(); fs = pzem_s.frequency(); pfs = pzem_s.pf();
  vt = pzem_t.voltage(); it = pzem_t.current(); pt = pzem_t.power(); et = pzem_t.energy(); ft = pzem_t.frequency(); pft = pzem_t.pf();

  // Hitung VLL
  v_rs = isnan(vr) || isnan(vs) ? 0.0 : sqrt(pow(vr, 2) + pow(vs, 2) + vr * vs);
  v_st = isnan(vs) || isnan(vt) ? 0.0 : sqrt(pow(vs, 2) + pow(vt, 2) + vs * vt);
  v_tr = isnan(vt) || isnan(vr) ? 0.0 : sqrt(pow(vt, 2) + pow(vr, 2) + vt * vr);
}

void kirimDataKeServer() {
  // Ganti nilai NaN (error) dengan 0 sebelum mengirim
  if(isnan(vr)) vr=0; if(isnan(ir)) ir=0; if(isnan(pr)) pr=0; if(isnan(er)) er=0; if(isnan(fr)) fr=0; if(isnan(pfr)) pfr=0;
  if(isnan(vs)) vs=0; if(isnan(is)) is=0; if(isnan(ps)) ps=0; if(isnan(es)) es=0; if(isnan(fs)) fs=0; if(isnan(pfs)) pfs=0;
  if(isnan(vt)) vt=0; if(isnan(it)) it=0; if(isnan(pt)) pt=0; if(isnan(et)) et=0; if(isnan(ft)) ft=0; if(isnan(pft)) pft=0;

  // Buat string payload
  String payload = endpoint + "?";
  payload += "tegangan_r=" + String(vr) + "&arus_r=" + String(ir) + "&daya_r=" + String(pr) + "&energi_r=" + String(er) + "&frekuensi_r=" + String(fr) + "&faktor_daya_r=" + String(pfr);
  payload += "&tegangan_s=" + String(vs) + "&arus_s=" + String(is) + "&daya_s=" + String(ps) + "&energi_s=" + String(es) + "&frekuensi_s=" + String(fs) + "&faktor_daya_s=" + String(pfs);
  payload += "&tegangan_t=" + String(vt) + "&arus_t=" + String(it) + "&daya_t=" + String(pt) + "&energi_t=" + String(et) + "&frekuensi_t=" + String(ft) + "&faktor_daya_t=" + String(pft);
  payload += "&voltage_rs=" + String(v_rs) + "&voltage_st=" + String(v_st) + "&voltage_tr=" + String(v_tr);
  
  if (client.connect(ipServer, portServer)) {
    client.println("GET " + payload + " HTTP/1.1");
    client.println("Host: " + String(ipServer[0]) + "." + String(ipServer[1]) + "." + String(ipServer[2]) + "." + String(ipServer[3]));
    client.println("Connection: close");
    client.println();
    client.stop();
    statusKirimTerakhir = "OK";
  } else {
    statusKirimTerakhir = "Gagal";
  }
}

bool handleConnectionError() {
  lcd.clear();
  lcd.print("Gagal terhubung!");
  lcd.setCursor(0, 2);
  lcd.print("1: OK (Offline)");
  lcd.setCursor(0, 3);
  lcd.print("2: Coba Lagi");
  
  while (true) {
    char key = 0;
    if (key == '1') { // Pilih Mode Offline
      return true;
    }
    if (key == '2') { // Pilih Coba Lagi
      lcd.clear();
      lcd.print("Mencoba lagi...");
      Ethernet.begin(mac, ipArduino);
      delay(1000);
      if (Ethernet.linkStatus() != LinkOFF && Ethernet.localIP() != IPAddress(0,0,0,0)) {
        lcd.clear();
        lcd.print("Berhasil terhubung!");
        delay(2000);
        return false; // Berhasil, tidak jadi mode offline
      } else {
        // Jika masih gagal, tampilkan menu lagi
        lcd.clear();
        lcd.print("Masih gagal!");
        lcd.setCursor(0, 2);
        lcd.print("1: OK (Offline)");
        lcd.setCursor(0, 3);
        lcd.print("2: Coba Lagi");
      }
    }
  }
}
