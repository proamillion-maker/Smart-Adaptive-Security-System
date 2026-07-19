#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HX711.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <ArduinoJson.h> 
#include <esp_now.h> // Thư viện ESP-NOW tích hợp

// ================= CẤU HÌNH WI-FI & THINGSBOARD =================
const char* ssid = "OPPO";                  
const char* password = "123456790";                
const char* thingsboard_server = "mqtt.thingsboard.cloud"; 
const char* access_token = "D3gS54hEZnVcpJzJD3H5";     

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ================= CẤU HÌNH ESP-NOW 2 CHIỀU =================
// ĐỊA CHỈ MAC CỦA BOARD CODE 1 (Bạn của bạn)
uint8_t mac_code1[] = {0xfc, 0xe8, 0xc0, 0xe1, 0x11, 0x48}; 

// 1. Cấu trúc dữ liệu GỬI đi (Khớp với struct_esp1 ở Code 1)
typedef struct __attribute__((packed)) struct_esp1 {
    uint8_t id;
    float x; // Gửi Cân nặng
    int y;   // Gửi Trạng thái cảm biến hồng ngoại (IR)
} struct_esp1;
struct_esp1 dataToSend;

// 2. Cấu trúc dữ liệu NHẬN về (Khớp với struct_message_Node3 ở Code 1)
typedef struct struct_message_Node3 {
  int TotalRed;
  int TotalGreen;
  int DoCon;
  int Nuoc;
} struct_message_Node3;
struct_message_Node3 incomingData;
esp_now_peer_info_t peerInfo;

// Callback khi GỬI dữ liệu qua ESP-NOW
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Trang thai gui ESP-NOW: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Thanh cong" : "That bai");
}

// Callback khi NHẬN dữ liệu qua ESP-NOW
void OnDataRecv(const esp_now_recv_info_t * mac, const uint8_t *incomingDataPtr, int len) {
  if (len == sizeof(struct_message_Node3)) {
    memcpy(&incomingData, incomingDataPtr, sizeof(incomingData));
    Serial.println("--- NHAN DU LIEU TU NODE 3 ---");
    Serial.printf("Mau Do: %d | Mau Xanh: %d | Do Con: %d | Nuoc: %d\n", 
                  incomingData.TotalRed, incomingData.TotalGreen, incomingData.DoCon, incomingData.Nuoc);
    Serial.println("------------------------------");
  } else {
    Serial.println("Du lieu nhan kich thuoc khong hop le!");
  }
}

// --- CẤU HÌNH OLED (Chạy I2C phụ trên chân 16, 17) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- CẤU HÌNH RFID RC522 ---
#define SS_PIN    5
#define RST_PIN   4
MFRC522 mfrc522(SS_PIN, RST_PIN);

// --- CẤU HÌNH LOAD CELL & HX711 ---
#define LOADCELL_DOUT_PIN 21
#define LOADCELL_SCK_PIN  22
HX711 scale;
float calibration_factor = 69444.45; // Đã giữ nguyên hệ số hiệu chuẩn chuẩn của bạn

// --- CẤU HÌNH SERVO ---
#define SERVO_PIN 2
Servo gateServo;

// --- CẤU HÌNH CÁC CẢM BIẾN VÀ ĐÈN ---
#define IR_PIN 13
#define LED_RED    25
#define LED_YELLOW 26
#define LED_GREEN  27

// --- CẤU HÌNH CÁC LINH KIỆN CÓ CÒI & CẢM BIẾN LỬA CHUẨN ---
#define FLAME_PIN  33   // Chân tín hiệu cảm biến lửa GPIO 33
#define BUZZER_PIN 32   
#define DHTPIN     15   
#define DHTTYPE    DHT11
DHT dht(DHTPIN, DHTTYPE);

String validUID = "E7 C1 12 07"; 
bool dangTrongTienTrinh = false; 
bool heThongBiKhoaDoChay = false; 

String current_led = "OFF"; 
int current_servo = 0;       
float t = 28.5; 
float h = 60.0; 

// Hàm kết nối Wi-Fi (Đặt chế độ WIFI_STA bắt buộc để chạy song song ESP-NOW)
void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Dang ket noi den WiFi: ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA); 
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi da ket noi!");
  Serial.print("Kenh WiFi (Channel) hien tai: ");
  Serial.println(WiFi.channel());
}

// ================= HÀM XỬ LÝ LỆNH TỪ NÚT NHẤN THINGSBOARD (RPC) =================
void on_callback(char* topic, byte* payload, unsigned int length) {
  char json[200];
  memcpy(json, payload, length);
  json[length] = '\0';
  
  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) return;

  String method = doc["method"];
  
  if (method == "resetFire") {
    heThongBiKhoaDoChay = false;
    digitalWrite(BUZZER_PIN, LOW);
    dieuKhienServo(0);
    dieuKhienDen("OFF");
    Serial.println("-> DA RESET BAO CHAY TU THINGSBOARD!");
    
    String responseTopic = String(topic);
    responseTopic.replace("request", "response");
    mqttClient.publish(responseTopic.c_str(), "{\"status\":\"ok\"}");
  }
}

void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Dang ket noi MQTT ThingsBoard...");
    if (mqttClient.connect("ESP32_TramCan", access_token, NULL)) {
      Serial.println("Thanh cong!");
      mqttClient.subscribe("v1/devices/me/rpc/request/+"); 
    } else {
      Serial.print("That bai, ma loi: ");
      Serial.print(mqttClient.state());
      Serial.println(" Thu lai sau 5 giay...");
      delay(5000);
    }
  }
}

// ================= HÀM GỬI TRẠNG THÁI I/O LÊN CLOUD & ESP-NOW =================
void guiTrangThaiHeThong(String uid, float weight, String status) {
  if (!mqttClient.connected()) reconnect();
  int irState = digitalRead(IR_PIN);

  // 1. Đồng bộ dữ liệu telemetry lên ThingsBoard
  String payload = "{";
  payload += "\"uid\":\"" + uid + "\",";
  payload += "\"weight\":" + String(weight, 1) + ",";
  payload += "\"status\":\"" + status + "\",";
  payload += "\"temperature\":" + String(t, 1) + ",";
  payload += "\"humidity\":" + String(h, 1) + ",";
  payload += "\"ir_sensor\":" + String(irState) + ",";
  payload += "\"flame_detected\":" + String(heThongBiKhoaDoChay ? 1 : 0) + ","; 
  payload += "\"active_led\":\"" + current_led + "\",";
  payload += "\"buzzer\":" + String(digitalRead(BUZZER_PIN)) + ",";
  payload += "\"barrier_angle\":" + String(current_servo);
  payload += "}";

  char attributes[300];
  payload.toCharArray(attributes, 300);
  mqttClient.publish("v1/devices/me/telemetry", attributes);
  Serial.println("Da dong bo I/O len ThingsBoard: " + payload);

  // 2. Đồng thời gửi dữ liệu qua ESP-NOW sang node của bạn bạn
  dataToSend.id = 1;               
  dataToSend.x = weight;      
  dataToSend.y = irState;          
  esp_now_send(mac_code1, (uint8_t *) &dataToSend, sizeof(dataToSend));
}

void dieuKhienDen(String mau) {
  current_led = mau;
  digitalWrite(LED_RED, (mau == "RED") ? HIGH : LOW);
  digitalWrite(LED_YELLOW, (mau == "YELLOW") ? HIGH : LOW);
  digitalWrite(LED_GREEN, (mau == "GREEN") ? HIGH : LOW);
}

void dieuKhienServo(int goc) {
  current_servo = goc;
  gateServo.write(goc);
}

void setup() {
  Serial.begin(115200);
  
  // 1. Khởi tạo OLED trước tiên trên Bus I2C riêng (16, 17)
  Wire.begin(16, 17);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Khong tim thay OLED"));
    for(;;);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 10);
  display.println("Khoi dong he thong...");
  display.display();
  delay(1000); 

  // 2. Kết nối Wifi & MQTT
  setup_wifi();
  mqttClient.setServer(thingsboard_server, 1883);
  mqttClient.setCallback(on_callback); 

  // 3. Khởi tạo ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Loi khoi tao ESP-NOW");
    return;
  }
  esp_now_register_send_cb(esp_now_send_cb_t(OnDataSent));
  esp_now_register_recv_cb(esp_now_recv_cb_t(OnDataRecv));
  
  // Thiết lập liên kết kênh ESP-NOW với node nhận
  memcpy(peerInfo.peer_addr, mac_code1, 6);
  peerInfo.channel = 0; // Tự động khóa chung kênh với WiFi hiện tại
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK){
    Serial.println("Loi khi them Peer ESP-NOW");
    return;
  }

  // 4. Khởi tạo các ngoại vi khác
  SPI.begin();
  mfrc522.PCD_Init();
  
  dht.begin(); 

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
  scale.set_scale(calibration_factor);
  scale.tare(); 

  ESP32PWM::allocateTimer(0);
  gateServo.setPeriodHertz(50); 
  gateServo.attach(SERVO_PIN, 500, 2400);
  dieuKhienServo(0); 

  pinMode(IR_PIN, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  
  pinMode(FLAME_PIN, INPUT); // Nhận mức logic ổn định (Đã sửa đổi logic trong Loop)
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); 

  dieuKhienDen("OFF");
}

void loop() {
  if (!mqttClient.connected()) reconnect();
  mqttClient.loop();

  // Đọc DHT11 giãn cách 3 giây/lần tránh xung đột I2C với OLED
  static unsigned long thoiGianDocDHT = 0;
  if (millis() - thoiGianDocDHT > 3000) {
    thoiGianDocDHT = millis();
    float moi_t = dht.readTemperature();
    float moi_h = dht.readHumidity();
    if (!isnan(moi_t) && !isnan(moi_h)) { 
      t = moi_t; 
      h = moi_h; 
    }
  }

  int flameVal = digitalRead(FLAME_PIN);

  // ================= 🚨 KỊCH BẢN CHÁY NỔ KHẨN CẤP (LOGIC CHUẨN ACTIVE HIGH) =================
  if (flameVal == HIGH || heThongBiKhoaDoChay) { 
    heThongBiKhoaDoChay = true; 
    dieuKhienServo(90);  
    
    static unsigned long thoiGianCoi = 0;
    static bool trangThaiCoi = false;
    if (millis() - thoiGianCoi > 150) { 
      thoiGianCoi = millis();
      trangThaiCoi = !trangThaiCoi;
      digitalWrite(BUZZER_PIN, trangThaiCoi ? HIGH : LOW); 
      digitalWrite(LED_RED, trangThaiCoi ? HIGH : LOW);
      digitalWrite(LED_YELLOW, LOW);
      digitalWrite(LED_GREEN, LOW);
    }
    current_led = "FLASH_RED_EMERGENCY";

    display.clearDisplay();
    display.setTextSize(2); display.setCursor(10, 0); display.println("HOA HOAN!");
    display.setTextSize(1);
    display.setCursor(0, 25); display.println("TU DONG THOAT HIEM");
    display.setCursor(0, 42); display.println("T: " + String(t, 1) + "C | H: " + String(h, 0) + "%");
    display.setCursor(0, 55); display.println("CONG DANG MO TOANG");
    display.display();

    static unsigned long thoiGianGuiMqtt = 0;
    if (millis() - thoiGianGuiMqtt > 2000) {
      thoiGianGuiMqtt = millis();
      guiTrangThaiHeThong("EMERGENCY", 0.0, "HOA HOAN KHAN CAP!");
    }
    return; 
  }

  // ================= KỊCH BẢN 2: HOẠT ĐỘNG BÌNH THƯỜNG =================
  int irState = digitalRead(IR_PIN);
  String irStatus = (irState == LOW) ? "CO XE CHAN" : "TRONG TRAM";

  static unsigned long lastUpdate = 0;
  if (!dangTrongTienTrinh && (millis() - lastUpdate > 3000)) {
    lastUpdate = millis();
    dieuKhienDen("OFF");
    dieuKhienServo(0);
    digitalWrite(BUZZER_PIN, LOW); 
    guiTrangThaiHeThong("None", 0.0, "Cho Quet Thong");

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);   display.println("--- TRAM CAN XE ---");
    display.setCursor(0, 18);  display.println("Nhiet do: " + String(t, 1) + " C");
    display.setCursor(0, 32);  display.println("Do am:    " + String(h, 0) + " %");
    display.setCursor(0, 46);  display.println("IR: " + irStatus + " | MOI QUET THE...");
    display.display();
  }

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    delay(100);
    return;
  }

  dangTrongTienTrinh = true;
  String readUID = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    readUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
    readUID += String(mfrc522.uid.uidByte[i], HEX);
    if (i < mfrc522.uid.size - 1) readUID += " ";
  }
  readUID.toUpperCase();
  Serial.println("Quet thay card UID: " + readUID);

  // KỊCH BẢN: THẺ HỢP LỆ
  if (readUID == validUID) {
    dieuKhienDen("YELLOW"); 
    guiTrangThaiHeThong(readUID, 0.0, "Hop Le - Dang Xu Ly");
    hienThiGiaoDien("THE HOP LE!", "UID: " + readUID, "DANG TRONG TIEN TRINH...");
    delay(2000); 

    float weightKg = scale.get_units(10); 
    if (weightKg < 0) weightKg = 0; 
    float weightG = weightKg * 1000.0;
    String weightStr = String(weightG, 0) + " g";
    Serial.println("Trong tai do duoc: " + weightStr);

    if (weightG > 1000.0) {
      dieuKhienDen("RED"); dieuKhienServo(0);
      digitalWrite(BUZZER_PIN, HIGH); 
      guiTrangThaiHeThong(readUID, weightG, "Qua Tai");
      hienThiGiaoDien("UID: " + readUID, "CAN: " + weightStr, "QUA TAI! CAM QUA CUA");
      delay(5000); 
      digitalWrite(BUZZER_PIN, LOW);
    } 
    else {
      dieuKhienDen("GREEN"); dieuKhienServo(90); 
      guiTrangThaiHeThong(readUID, weightG, "Cho Phep Qua");
      hienThiGiaoDien("UID: " + readUID, "CAN: " + weightStr, "MO CUA! MOI QUA TRAM");
      delay(2000);         

      while (digitalRead(IR_PIN) == LOW) {
        if (digitalRead(FLAME_PIN) == HIGH) return; // Thoát ngay nếu có cháy giữa chừng khi xe đang qua
        hienThiGiaoDien("XE DANG DI QUA...", "IR: CO XE CHAN (0)", "VUI LONG CHO...");
        guiTrangThaiHeThong(readUID, weightG, "Xe Dang Di Qua");
        delay(500);
      }
      
      hienThiGiaoDien("XE DA QUA CUA", "IR: TRONG KHONG (1)", "DONG BARRIER...");
      delay(1000);        
      dieuKhienServo(0); dieuKhienDen("OFF"); 
      guiTrangThaiHeThong("None", 0.0, "Da Qua Tram - Cho Xe Moi");
      delay(1000);        
    }
  } 
  // KỊCH BẢN: THẺ LẠ
  else {
    dieuKhienDen("RED"); dieuKhienServo(0);
    digitalWrite(BUZZER_PIN, HIGH);
    guiTrangThaiHeThong(readUID, 0.0, "The Khong Hop Le");
    hienThiGiaoDien("THE KHONG HOP LE!", "UID: " + readUID, "TU CHOI TRUY CAP!");
    delay(4000); 
    digitalWrite(BUZZER_PIN, LOW);
  }

  dangTrongTienTrinh = false;
  mfrc522.PICC_HaltA();
}

void hienThiGiaoDien(String dong1, String dong2, String dong3) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println(dong1);
  display.setCursor(0, 22); display.println(dong2);
  display.setCursor(0, 45); display.println(dong3);
  display.display();
}