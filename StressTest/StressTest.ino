/*
  Blink

  Turns an LED on for one second, then off for one second, repeatedly.

  Most Arduinos have an on-board LED you can control. On the UNO, MEGA and ZERO
  it is attached to digital pin 13, on MKR1000 on pin 6. LED_BUILTIN is set to
  the correct LED pin independent of which board is used.
  If you want to know what pin the on-board LED is connected to on your Arduino
  model, check the Technical Specs of your board at:
  https://docs.arduino.cc/hardware/

  modified 8 May 2014
  by Scott Fitzgerald
  modified 2 Sep 2016
  by Arturo Guadalupi
  modified 8 Sep 2016
  by Colby Newman

  This example code is in the public domain.

  https://docs.arduino.cc/built-in-examples/basics/Blink/
*/

#include <ArduinoRS485.h>
#include <CAN.h>
#include <ZephyrClient.h>
#include "ZephyrEthernet.h"
ZephyrClient client;

char server[] = "www.google.com";  // name address web connection test
uint8_t msg_data[] = {0xCA, 0xFE, 0, 0, 0, 0, 0, 0}; // CAN message data
uint32_t const CAN_ID = 0x123;
bool led_state = true;

void client_connect() {
  // try to make a HTTP request
  if (client.connect(server, 80)) {
    client.println("GET /search?q=arduino HTTP/1.1");
    client.println("Host: www.google.com");
    client.println("Connection: close");
    client.println();
  } 
}

void rgb_loop() {
  static uint8_t rgb_state = 0;
  switch (rgb_state) {
    case 0:
      digitalWrite(LEDB, LOW);
      digitalWrite(LEDG, HIGH);
      digitalWrite(LEDR, HIGH);
      break;
    case 1:
      digitalWrite(LEDB, HIGH);
      digitalWrite(LEDG, LOW);
      digitalWrite(LEDR, HIGH);
      break;
    case 2:
      digitalWrite(LEDB, HIGH);
      digitalWrite(LEDG, HIGH);
      digitalWrite(LEDR, LOW);
      break;
  }
  rgb_state++;
  if (rgb_state > 2)
    rgb_state = 0;
}

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pins: LED_BUILTIN and RGB LEDs (active low)
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(LEDB, OUTPUT);
  pinMode(LEDG, OUTPUT);
  pinMode(LEDR, OUTPUT);
  digitalWrite(LEDB, HIGH);
  digitalWrite(LEDG, HIGH);
  digitalWrite(LEDR, HIGH);

  // init all the Serial ports
  Serial.begin(115200);
  Serial1.begin(115200);
  Serial2.begin(115200);
  RS485.begin(115200);

  delay(500); // wait for Serial to initialize

  // init CAN bus and Ethernet
  CAN.begin(CanBitRate::BR_500k);
  Ethernet.begin();
}

// the loop function runs over and over again forever
void loop() {
  static uint8_t msg_cnt = 0;
  char hello_world[24] = {0};
  
  digitalWrite(LED_BUILTIN, led_state); 
  rgb_loop();
  led_state = !led_state;
  
  snprintf(hello_world, sizeof(hello_world), "Hello World! %u ", static_cast<unsigned>(msg_cnt));
  Serial.println(hello_world);
  Serial1.println(hello_world);
  Serial2.println(hello_world);
  RS485.beginTransmission();
  RS485.println(hello_world);
  RS485.endTransmission();

  CanMsg msg(CanStandardId(CAN_ID), sizeof(msg_data), msg_data);
  memcpy((void *)(msg_data + 2), &msg_cnt, sizeof(msg_cnt));
  CAN.write(msg);

  if (msg_cnt < 0xFE)
    msg_cnt++;
  else
    msg_cnt = 0;

  client_connect();

  delay(1000);   // wait for a second
}
