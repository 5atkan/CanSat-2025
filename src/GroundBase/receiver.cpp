#include <SPI.h>
#include <LoRa.h>

// Simple settings
long freq = 433E6;   // Frequency for LoRa
int syncWord = 0x98;  // Sync word
int ledPin = 13;

void setup() {
  Serial.begin(9600);
  while (!Serial);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  Serial.println("LoRa Receiver");

  // Try to start LoRa, loop until it works
  while (!LoRa.begin(freq)) {
    Serial.println("LoRa init failed");
    delay(1000);
  }
  
  // Simple LoRa config
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(syncWord);
  Serial.println("LoRa is ready!");
}

void loop() {
  // Check if we got something from serial (like "ok")
  if (Serial.available() > 0) {
    String serialMsg = Serial.readStringUntil('\n');
    serialMsg.trim();
    if (serialMsg.equalsIgnoreCase("ok")) {
      // Send "OK" via LoRa
      LoRa.beginPacket();
      LoRa.print("OK");
      LoRa.endPacket();
      Serial.println("Sent OK via LoRa");
    }
  }

  // Check if LoRa received something
  int packetSize = LoRa.parsePacket();
  digitalWrite(ledPin, HIGH);
  digitalWrite(ledPin, LOW);
  if (packetSize) {
    while (LoRa.available()) {
      char c = LoRa.read();
      Serial.print(c);
    }
    Serial.println();
  }
}
