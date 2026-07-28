/*
  Turns all the outputs on for one second, repeatedly.

  Most Arduinos have an on-board LED you can control. On the UNO, MEGA and ZERO
  it is attached to digital pin 13, on MKR1000 on pin 6. LED_BUILTIN is set to
  the correct LED pin independent of which board is used.
  If you want to know what pin the on-board LED is connected to on your Arduino
  model, check the Technical Specs of your board at:
  https://docs.arduino.cc/hardware/

  This example code is in the public domain.
*/

#include "ZephyrEthernet.h"
#include <ArduinoRS485.h>
#include <CAN.h>
#include <ZephyrClient.h>
#include <ZephyrUDP.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/mii.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/phy.h>

ZephyrClient client;
ZephyrUDP udp_rx;
ZephyrUDP udp_tx;

char server[] = "www.google.com"; // name address web connection test
IPAddress loopback_ip(192, 168, 50, 2);
uint16_t const loopback_rx_port = 5555;
uint16_t const loopback_tx_port = 5556;
uint8_t msg_data[] = {0xCA, 0xFE, 0, 0, 0, 0, 0, 0}; // CAN message data
uint32_t const CAN_ID = 0x123;
bool led_state = true;
bool loopback_mode = false;

bool set_phy_loopback_ksz8081() {
  struct net_if *netif = net_if_get_first_ethernet();
  if (netif == nullptr) {
    Serial.println("Loopback setup failed: no ethernet netif");
    return false;
  }

  const struct device *phy_dev = net_eth_get_phy(netif);
  if (phy_dev == nullptr) {
    Serial.println("Loopback setup failed: no PHY device");
    return false;
  }

  /* Section 3.8.1 LOCAL (DIGITAL) LOOPBACK of KSZ8081MNX/RNB datasheet */
  uint32_t bmcr = MII_BMCR_LOOPBACK | MII_BMCR_SPEED_100 | MII_BMCR_DUPLEX_MODE;
  int ret = phy_write(phy_dev, MII_BMCR, bmcr);
  if (ret < 0) {
    Serial.print("Loopback setup failed: phy_write ret=");
    Serial.println(ret);
    return false;
  }

  delay(100);
  return true;
}

void http_connect() {
  // try to make a HTTP request
  if (client.connect(server, 80)) {
    client.println("GET /search?q=arduino HTTP/1.1");
    client.println("Host: www.google.com");
    client.println("Connection: close");
    client.println();
  }
}

void udp_loopback_send_and_readback(const char *payload) {
  if (!loopback_mode) {
    return;
  }

  if (!udp_tx.beginPacket(Ethernet.localIP(), loopback_rx_port)) {
    Serial.println("UDP beginPacket failed");
    return;
  }

  udp_tx.write((const uint8_t *)payload, strlen(payload));
  udp_tx.endPacket();

  unsigned long start = millis();
  while (millis() - start < 1000) {
    int packet_size = udp_rx.parsePacket();
    if (packet_size > 0) {
      char rx_buf[96] = {0};
      int n = udp_rx.read(rx_buf, sizeof(rx_buf) - 1);
      if (n > 0) {
        rx_buf[n] = '\0';
        Serial.print("UDP Loopback RX: ");
        Serial.println(rx_buf);
      }
      return;
    }
    delay(5);
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

  // init CAN bus and Ethernet
  CAN.begin(CanBitRate::BR_500k);
  Ethernet.begin();

  delay(500); // wait for Serial to initialize and eth link to go up

  if (Ethernet.linkStatus() == LinkOFF) {
    if (set_phy_loopback_ksz8081()) {
      Ethernet.begin(loopback_ip);
      udp_rx.begin(loopback_rx_port);
      udp_tx.begin(loopback_tx_port);
      loopback_mode = true;
    }
  }
}

// the loop function runs over and over again forever
void loop() {
  static uint8_t msg_cnt = 0;
  char hello_world[24] = {0};

  digitalWrite(LED_BUILTIN, led_state);
  rgb_loop();
  led_state = !led_state;

  snprintf(hello_world, sizeof(hello_world), "Hello World! %u ",
           static_cast<unsigned>(msg_cnt));
  Serial1.println(hello_world);
  Serial2.println(hello_world);
  RS485.beginTransmission();
  RS485.println(hello_world);
  RS485.endTransmission();

  CanMsg msg(CanStandardId(CAN_ID), sizeof(msg_data), msg_data);
  memcpy((void *)(msg_data + 2), &msg_cnt, sizeof(msg_cnt));
  CAN.write(msg, false); // non-blocking write

  if (msg_cnt < 0xFE)
    msg_cnt++;
  else
    msg_cnt = 0;

  if (!loopback_mode) {
    http_connect();
  } else {
    udp_loopback_send_and_readback(hello_world);
  }

  delay(1000); // wait for a second
}
