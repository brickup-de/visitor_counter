#include <AcksenButton.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <TM1637Display.h>

#define BUTTON_INCREASE 2 // green arcade button switch (to GND, INPUT_PULLUP)
#define LED_INCREASE 3    // green button LED (through resistor, active HIGH)
#define LED_DECREASE 4    // red button LED (through resistor, active HIGH)
#define BUTTON_DECREASE 5 // red arcade button switch (to GND, INPUT_PULLUP)
#define BUZZER 6          // passive buzzer -> tone()
#define RS485_DI 8
#define RS485_DE 9 // DE and RE tied together on D9 in the new wiring
#define RS485_RE 9 // kept as two defines like v5; edit if wired separately
#define RS485_RO 10
#define DISPLAY_CLK A5 // Grove TM1637
#define DISPLAY_DIO A4

#define BUTTON_DEBOUNCE_INTERVAL 20
#define LED_FLASH_MS 200           // button LED feedback on press
#define SYNC_SEND_INTERVAL_MS 1000 // periodic send of local count
#define LINK_TIMEOUT_MS 5000       // link considered lost after this silence
#define LINK_BLINK_MS 500          // red LED blink half-period while link lost
#define ALARM_INTERVAL_MS 5000     // warning chirps repeat while link lost
#define ALARM_TONE_HZ 2000
#define ALARM_TONE_MS 100
#define ALARM_CHIRP_GAP_MS 150 // start of 2nd chirp after start of 1st

AcksenButton btnIncrease(BUTTON_INCREASE, ACKSEN_BUTTON_MODE_NORMAL, BUTTON_DEBOUNCE_INTERVAL, INPUT_PULLUP);
AcksenButton btnDecrease(BUTTON_DECREASE, ACKSEN_BUTTON_MODE_NORMAL, BUTTON_DEBOUNCE_INTERVAL, INPUT_PULLUP);
TM1637Display disTotal(DISPLAY_CLK, DISPLAY_DIO);
SoftwareSerial rs485(RS485_RO, RS485_DI);

int32_t local = 0;
int32_t remote = 0;
int32_t total = 0;

bool ledIncreaseFlash = false;
bool ledDecreaseFlash = false;
unsigned long ledIncreaseFlashStart = 0;
unsigned long ledDecreaseFlashStart = 0;

unsigned long lastSendMs = 0;
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

  pinMode(BUZZER, OUTPUT);
}

void loop() {
  unsigned long now = millis();

  btnIncrease.refreshStatus();
  btnDecrease.refreshStatus();

  if (btnIncrease.onPressed()) {
    local++;
    ledIncreaseFlash = true;
    ledIncreaseFlashStart = now;
    refresh();
    sendLocal();
  }

  if (btnDecrease.onPressed()) {
    local--;
    ledDecreaseFlash = true;
    ledDecreaseFlashStart = now;
    refresh();
    sendLocal();
  }

  if (rs485.available() > 0) {
    remote = rs485.parseInt();
    rs485.read(); // -> "|"
    lastRemoteMs = millis();
    remoteSeen = true;
    Serial.println(remote);
    refresh();
  } else if (now - lastSendMs >= SYNC_SEND_INTERVAL_MS) {
    sendLocal();
  }

  if (ledIncreaseFlash && now - ledIncreaseFlashStart >= LED_FLASH_MS) {
    ledIncreaseFlash = false;
  }
  if (ledDecreaseFlash && now - ledDecreaseFlashStart >= LED_FLASH_MS) {
    ledDecreaseFlash = false;
  }

  bool linkUp = remoteSeen && (now - lastRemoteMs < LINK_TIMEOUT_MS);

  bool redOn = ledDecreaseFlash;
  if (!linkUp) {
    // blink overrides/coexists with the press flash
    redOn = redOn || ((now / LINK_BLINK_MS) % 2 == 0);

    if (now - lastAlarmMs >= ALARM_INTERVAL_MS) {
      lastAlarmMs = now;
      secondChirpPending = true;
      tone(BUZZER, ALARM_TONE_HZ, ALARM_TONE_MS);
    } else if (secondChirpPending && now - lastAlarmMs >= ALARM_CHIRP_GAP_MS) {
      secondChirpPending = false;
      tone(BUZZER, ALARM_TONE_HZ, ALARM_TONE_MS);
    }
  } else {
    secondChirpPending = false;
  }

  digitalWrite(LED_INCREASE, ledIncreaseFlash ? HIGH : LOW);
  digitalWrite(LED_DECREASE, redOn ? HIGH : LOW);
}

void sendLocal() {
  lastSendMs = millis();

  digitalWrite(RS485_DE, HIGH);
  digitalWrite(RS485_RE, HIGH);

  rs485.print(local);
  rs485.print("|");
  rs485.flush();

  digitalWrite(RS485_DE, LOW);
  digitalWrite(RS485_RE, LOW);

  Serial.print("Sent ");
  Serial.println(local);
}

void refresh() {
  total = abs(local - remote);
  disTotal.showNumberDec(total, true);
}
