#include <Adafruit_Sensor.h>
#include <Adafruit_BMP3XX.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <Arduino.h>
#include <TinyGPS++.h>

// ----- Pin and Basic Settings -----
#define UV_PIN A0         // UV sensor input
#define GPS_RX_PIN 19     // GPS receive pin
#define GPS_TX_PIN 18     // GPS transmit pin

// LoRa Settings
const long LORA_FREQ = 433E6; // frequency
const int LORA_CS = 10;       // chip select pin
const int LORA_RST = 8;       // reset pin
const int LORA_IRQ = 2;       // IRQ pin
const int LED_PIN = 13;       // onboard LED
const int SYNC_WORD = 0x98;   // sync word

int loopCount = 0;       // counter
bool sendCoordsOnly = false;  // if true, only send coordinates

// ----- Sensor Objects -----
Adafruit_BMP3XX bmp;
TinyGPSPlus gps;

// CO2 sensor command and value
const uint8_t CO2_CMD[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};
int co2Value = 0;

// Function to calibrate CO2 sensor at 400 ppm
void doCalibrate() {
  uint8_t calCmd[] = {0xFF, 0x01, 0x87, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78};
  Serial2.write(calCmd, 9);
  Serial.println("400ppm calibration sent");
}

unsigned long lastSendTime = 0;
const unsigned long SEND_INTERVAL = 1000; // send data every second

void setup() {
  Serial.begin(9600);      // Debug serial
  Serial1.begin(9600);     // GPS serial
  Serial2.begin(9600);     // CO2 sensor serial

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Setup LoRa module
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed");
    while (1);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(SYNC_WORD);

  // Let everyone know LoRa is running
  LoRa.beginPacket();
  LoRa.print("LoRa init");
  LoRa.endPacket();

  // Setup BMP390
  if (!bmp.begin_I2C()) {
    Serial.println("BMP390 not found!");
    while (1);
  }
  LoRa.beginPacket();
  LoRa.print("BMP390 ok");
  LoRa.endPacket();

  bmp.setTemperatureOversampling(BMP3_NO_OVERSAMPLING);
  bmp.setPressureOversampling(BMP3_OVERSAMPLING_4X);
  bmp.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_1);
  bmp.setOutputDataRate(BMP3_ODR_200_HZ);

  // 5-minute countdown with blinking LED
  LoRa.beginPacket();
  LoRa.print("Starting in 5 mins");
  LoRa.endPacket();
  Serial.println("Starting in 5 mins");
  for (int i = 0; i < 5; i++) {
    Serial.println(5 - i);
    LoRa.beginPacket();
    LoRa.print(String(5 - i) + " mins left");
    LoRa.endPacket();
    unsigned long startTime = millis();
    while (millis() - startTime < 60000) {
      digitalWrite(LED_PIN, HIGH);
      delay(100);
      digitalWrite(LED_PIN, LOW);
      delay(100);
    }
  }

  doCalibrate();
  LoRa.beginPacket();
  LoRa.print("Systems go");
  LoRa.endPacket();
}

void loop() {
  // Check if any LoRa message is received
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String inMsg = "";
    while (LoRa.available()) {
      char ch = (char)LoRa.read();
      inMsg += ch;
    }
    inMsg.trim();
    Serial.print("LoRa msg: ");
    Serial.println(inMsg);
    // If we get "OK", switch to coordinate-only mode
    if (inMsg == "OK") {
      sendCoordsOnly = true;
      Serial.println("Switched to coordinates-only mode!");
    }
  }
  
  // Default sensor values
  float temp = 0, pres = 0, bmpAlt = 0, uvIndex = 0;
  double lat = 0, lng = 0;
  float gpsAlt = 0, spd = 0;

  // Get full telemetry if not in coordinate-only mode
  if (!sendCoordsOnly) {
    if (bmp.performReading()) {
      temp = bmp.temperature;
      pres = bmp.pressure / 100.0; // convert to hPa
      bmpAlt = bmp.readAltitude(1013.25);
    } else {
      Serial.println("BMP reading error");
    }
    int uvRaw = analogRead(UV_PIN);
    float uvVolt = uvRaw * (5.0 / 1023.0);
    uvIndex = (uvVolt * 1000.0) / 307.0;
    
    // Try to get CO2 reading every other loop
    if (loopCount % 2 == 0) {
      Serial2.write(CO2_CMD, 9);
      delay(100);
      uint8_t resp[9];
      if (Serial2.available()) {
        Serial2.readBytes(resp, 9);
        if (resp[0] == 0xFF && resp[1] == 0x86) {
          co2Value = (resp[2] << 8) | resp[3];
        }
      }
    }
  }
  
  // Get GPS data
  unsigned long gpsTime = millis();
  while (millis() - gpsTime < 100) {
    while (Serial1.available() > 0) {
      char c = Serial1.read();
      gps.encode(c);
    }
  }
  if (gps.location.isValid()) {
    lat = gps.location.lat();
    lng = gps.location.lng();
    gpsAlt = gps.altitude.meters();
    spd = gps.speed.kmph();
  }

  // Send data every second
  if (millis() - lastSendTime >= SEND_INTERVAL) {
    String outData = "";
    if (!sendCoordsOnly) {
      // Send full telemetry
      outData = String(temp) + "," + String(pres) + "," + String(bmpAlt) + "," +
                String(uvIndex) + "," + String(co2Value) + "," +
                String(lat, 6) + "," + String(lng, 6) + "," +
                String(gpsAlt, 6) + "," + String(spd);
    } else {
      // Only send coordinates
      outData = String(lat, 6) + "," + String(lng, 6) + "," +
                String(gpsAlt, 6) + "," + String(spd, 6);
    }
    Serial.println(outData);
    
    digitalWrite(LED_PIN, HIGH);
    LoRa.beginPacket();
    LoRa.print(outData);
    LoRa.endPacket();
    digitalWrite(LED_PIN, LOW);

    loopCount++;
    lastSendTime = millis();
  }
}
