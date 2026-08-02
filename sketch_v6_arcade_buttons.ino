#include <AcksenButton.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <TM1637Display.h>

// ########################################## //
//                   WIRING                   //
//       arduino port of each component       //
// ########################################## //
#define BUTTON_INCREASE 2
#define BUTTON_INCREASE_LED 3
#define BUTTON_DECREASE 5
#define BUTTON_DECREASE_LED 4
#define DISPLAY_CLK A5
#define DISPLAY_DIO A4
#define SOUND_BUZZER 6
#define RS485_DI 8
#define RS485_DE 9
#define RS485_RE 10
#define RS485_RO 11

// ########################################## //
//                   CONFIG                   //
//  adjustable values inside of the program   //
// ########################################## //
#define BUTTON_DEBOUNCE_MS 20
#define CONNECTION_BAUD_RATE 9600
#define SERIAL_BAUD_RATE 9600

// ########################################## //
//                SETUP & LOOP                //
//  adjustable values inside of the program   //
// ########################################## //
void setup() {
  Serial.begin(SERIAL_BAUD_RATE);

  setupButtons();
  setupConnection();
  setupDisplay();
  setupSound();
}

void loop() {
  loopButtons();
  loopConnection();
  loopDisplay();
  loopSound();
}

// ########################################## //
//                   COUNT                    //
// ########################################## //
int32_t localCount = 0;
int32_t remoteCount = 0;
int32_t totalCount() {
  return localCount + remoteCount;
}

void changeLocalCount(int8_t delta) {
  setLocalCount(localCount + delta);
}

void setLocalCount(int32_t local) {
  if (localCount == local)
    return;

  localCount = local;
  Serial.print("setLocalCount: ");
  Serial.println(localCount);
}

void setRemoteCount(int32_t remote) {
  if (remoteCount == remote)
    return;

  remoteCount = remote;
  Serial.print("setRemoteCount: ");
  Serial.println(remoteCount);
}

// ########################################## //
//                  BUTTONS                   //
//      Arcade switches with an LED each      //
// ########################################## //
AcksenButton btnIncrease(BUTTON_INCREASE, ACKSEN_BUTTON_MODE_NORMAL, BUTTON_DEBOUNCE_MS, INPUT_PULLUP);
AcksenButton btnDecrease(BUTTON_DECREASE, ACKSEN_BUTTON_MODE_NORMAL, BUTTON_DEBOUNCE_MS, INPUT_PULLUP);

void setupButtons() {
  pinMode(BUTTON_INCREASE_LED, OUTPUT);
  pinMode(BUTTON_DECREASE_LED, OUTPUT);
  digitalWrite(BUTTON_INCREASE_LED, LOW);
  digitalWrite(BUTTON_DECREASE_LED, LOW);
}

void loopButtons() {
  btnIncrease.refreshStatus();
  btnDecrease.refreshStatus();

  if (btnIncrease.onPressed())
    changeLocalCount(+1);
  if (btnDecrease.onPressed())
    changeLocalCount(-1);
}

// ########################################## //
//                  DISPLAY                   //
//         4-digit 7-segment-display          //
// ########################################## //
TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);

void setupDisplay() {
  display.setBrightness(1);
  displayNumber(0);
}

void loopDisplay() {
  int32_t count = totalCount();
  displayNumber(count);
}

void displayNumber(int32_t number) {
  static int32_t displayed = 8888;
  if (displayed != number) {
    display.showNumberDec(number, false);
    displayed = number;
  }
}

// ########################################## //
//                 CONNECTION                 //
//            between the counters            //
// ########################################## //
SoftwareSerial connection(RS485_RO, RS485_DI);

void setupConnection() {
  pinMode(RS485_DE, OUTPUT);
  pinMode(RS485_RE, OUTPUT);
  setSending(false);

  connection.begin(9600);
}

void setSending(bool transmit) {
  digitalWrite(RS485_DE, transmit ? HIGH : LOW);
  digitalWrite(RS485_RE, transmit ? HIGH : LOW);
}

void loopConnection() {
  if (connection.available() > 0) {
    receiveRemoteCount();
  } else {
    sendLocalCountFrequently();
  }
}

void sendLocalCountFrequently() {
  static int32_t iteration = 0;

  if (iteration++ > 10000) {
    sendLocalCount();
    iteration = 0;
  }
}

void sendLocalCount() {
  setSending(true);
  connection.print(localCount);
  connection.print("|");
  connection.flush();
  setSending(false);

  Serial.print("sendLocalCount: ");
  Serial.println(localCount);
}

void receiveRemoteCount() {
  int32_t remote = connection.parseInt();
  char separator = connection.read();
  setRemoteCount(remote);

  Serial.print("receiveRemoteCount: ");
  Serial.println(localCount);
}

// ########################################## //
//                    SOUND                   //
//   passive buzzer to produce beep sounds    //
// ########################################## //

void setupSound() {
  pinMode(SOUND_BUZZER, OUTPUT);
}

void loopSound() {
}
