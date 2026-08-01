#include <Arduino.h>
#include <AcksenButton.h>
#include <TM1637Display.h>
#include <SoftwareSerial.h>

#define BTN_DEBOUNCE_MS 20
#define BTN_INCREASE 7
#define BTN_DECREASE 2
#define DISPLAY_LOCAL_CLK 5
#define DISPLAY_LOCAL_DIO 6
#define DISPLAY_TOTAL_CLK 3
#define DISPLAY_TOTAL_DIO 4
#define RS485_DI 11
#define RS485_DE 10
#define RS485_RE 9
#define RS485_R0 8

AcksenButton btnIncrease(BTN_INCREASE, ACKSEN_BUTTON_MODE_NORMAL, BTN_DEBOUNCE_MS, INPUT_PULLUP);
AcksenButton btnDecrease(BTN_DECREASE, ACKSEN_BUTTON_MODE_NORMAL, BTN_DEBOUNCE_MS, INPUT_PULLUP);
TM1637Display disLocal(DISPLAY_LOCAL_CLK, DISPLAY_LOCAL_DIO);
TM1637Display disTotal(DISPLAY_TOTAL_CLK, DISPLAY_TOTAL_DIO);
SoftwareSerial rs485(RS485_R0, RS485_DI);

int32_t local = 0;
int32_t remote = 0;
int32_t total = 0;
int32_t synci = 0;

void setup() {
  disLocal.setBrightness(4);
  disLocal.showNumberDec(local, true);

  disTotal.setBrightness(1);
  disTotal.showNumberDec(total, true);

  Serial.begin(9600);
  rs485.begin(9600);

  pinMode(RS485_DE, OUTPUT);
  pinMode(RS485_RE, OUTPUT);
  digitalWrite(RS485_DE, LOW);
  digitalWrite(RS485_RE, LOW);
}

void loop() {
  btnIncrease.refreshStatus();
  btnDecrease.refreshStatus();
  if (btnIncrease.onPressed() == true) {
    local++;
    refresh();
  }
  if (btnDecrease.onPressed() == true) {
    local--;
    refresh();
  }

  if (rs485.available() > 0) {
    remote = rs485.parseInt();
    rs485.read(); // -> "|"
    Serial.println(remote);
    refresh();
  } else if (synci++ > 10000) {
    synci = 0;
    digitalWrite(RS485_DE, HIGH);
    digitalWrite(RS485_RE, HIGH);

    rs485.print(local);
    rs485.print("|");
    Serial.print("Sent ");
    Serial.println(local);

    digitalWrite(RS485_DE, LOW);
    digitalWrite(RS485_RE, LOW);
  }
}

void refresh() {
  total = abs(local - remote);
  disLocal.showNumberDec(local, true);
  disTotal.showNumberDec(total, true);
}
