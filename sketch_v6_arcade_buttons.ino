#include <AcksenButton.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <TM1637Display.h>

#define BTN_INCREASE 2 // green button
#define BTN_DECREASE 5 // red button
#define LED_INCREASE 3 // green LED
#define LED_DECREASE 4 // red LED
#define DISPLAY_CLK A5 // 4x7-segment-display
#define DISPLAY_DIO A4
#define TONE_BUZZER 6
#define RS485_DI 8
#define RS485_DE 9
#define RS485_RE 10
#define RS485_RO 11

#define BTN_DEBOUNCE_MS 0
#define LED_FLASH_MS 200
#define LINK_INTERVAL_MS 2000
#define LINK_INTERVAL_JITTER_MS 400
#define LINK_TIMEOUT_MS 10000
#define LINK_BLINK_MS 500
#define ALARM_TONE_HZ 2000
#define ALARM_TONE_MS 100
#define ALARM_TONE_GAP_MS 150
#define ALARM_REPEAT_MS 5000

AcksenButton btnIncrease(BTN_INCREASE, ACKSEN_BUTTON_MODE_NORMAL, BTN_DEBOUNCE_MS, INPUT_PULLUP);
AcksenButton btnDecrease(BTN_DECREASE, ACKSEN_BUTTON_MODE_NORMAL, BTN_DEBOUNCE_MS, INPUT_PULLUP);
TM1637Display disTotal(DISPLAY_CLK, DISPLAY_DIO);
SoftwareSerial rs485(RS485_RO, RS485_DI);

int32_t local = 0;
int32_t remote = 0;
int32_t total = 0;
int32_t synci = 0;

bool ledIncreaseFlash = false;
bool ledDecreaseFlash = false;
unsigned long ledIncreaseFlashStart = 0;
unsigned long ledDecreaseFlashStart = 0;

unsigned long lastSendMs = 0;
unsigned long sendIntervalMs = LINK_INTERVAL_MS;
unsigned long lastRemoteMs = 0;
bool remoteSeen = false;

unsigned long lastAlarmMs = 0;
bool secondChirpPending = false;

void setup() {
  disTotal.setBrightness(4);
  disTotal.showNumberDec(total, true);

  Serial.begin(9600);
  rs485.begin(9600);

  pinMode(RS485_DE, OUTPUT);
  pinMode(RS485_RE, OUTPUT);
  digitalWrite(RS485_DE, LOW);
  digitalWrite(RS485_RE, LOW);

  pinMode(LED_INCREASE, OUTPUT);
  pinMode(LED_DECREASE, OUTPUT);
  digitalWrite(LED_INCREASE, LOW);
  digitalWrite(LED_DECREASE, LOW);

  pinMode(TONE_BUZZER, OUTPUT);

  randomSeed(analogRead(A0));
}

void loop() {
  btnIncrease.refreshStatus();
  btnDecrease.refreshStatus();

  if (btnIncrease.onPressed()) {
    local++;
    Serial.print("local/increase: ");
    Serial.println(local);
    refresh();
  }

  if (btnDecrease.onPressed()) {
    local--;
    Serial.print("local/decrease: ");
    Serial.println(local);
    refresh();
  }

  if (rs485.available() > 0) {
    remote = rs485.parseInt();
    rs485.read(); // -> "|"
    Serial.print("remote/receive: ");
    Serial.println(remote);
    refresh();
  } else if (synci++ > 10000) {
    synci = 0;
    sendLocal();
  }
}

void sendLocal() {
  digitalWrite(RS485_DE, HIGH);
  digitalWrite(RS485_RE, HIGH);

  rs485.print(local);
  rs485.print("|");
  rs485.flush();

  digitalWrite(RS485_DE, LOW);
  digitalWrite(RS485_RE, LOW);

  Serial.print("local/send: ");
  Serial.println(local);
}

void refresh() {
  total = abs(local - remote);
  disTotal.showNumberDec(total, false);
}
