#include <AcksenButton.h>
#include <Arduino.h>
#include <EEPROM.h>
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
#define COMMUNICATION_DI 8
#define COMMUNICATION_DE 9
#define COMMUNICATION_RE 10
#define COMMUNICATION_RO 11
#define DISPLAY_CLK A5
#define DISPLAY_DIO A4
#define SOUND_BUZZER 6

// ########################################## //
//                   CONFIG                   //
//  adjustable values inside of the program   //
// ########################################## //

#define BACKUP_DEBOUNCE_MS 3000
#define BUTTON_DEBOUNCE_MS 20
#define CONNECTION_BAUD_RATE 9600
#define DISPLAY_LEADING_ZEROS false
#define SERIAL_BAUD_RATE 9600

// ########################################## //
//            TYPES & STRUCTURES              //
//      must be defined before the coding     //
// ########################################## //

typedef uint8_t  backup_checksum_t;
typedef uint32_t backup_id_t;
typedef uint16_t backup_index_t;
typedef int16_t  count_t;
typedef uint16_t backup_address_t;
typedef unsigned long time_t;

struct backup_fields {
  backup_id_t id;
  backup_checksum_t checksum;
  count_t count;
};

// ########################################## //
//                SETUP & LOOP                //
// ########################################## //

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);

  setupButtons();
  setupConnection();
  setupDisplay();
  setupSound();

  setupCount();
  setupBackup();
}

void loop() {
  loopBackup();
  loopButtons();
  loopConnection();
  loopCount();
  loopDisplay();
  loopSound();
}

// ########################################## //
//                   COUNT                    //
//       manages local/remote/total count     //
// ########################################## //

count_t countLocal;
count_t countRemote;
count_t countTotal() {
  return countLocal + countRemote;
}

time_t countLocalChangedAt;
time_t countRemoteChangedAt;
time_t countTotalChangedAt() {
  return max(countLocalChangedAt, countRemoteChangedAt);
}

void setupCount() {
  countSetLocal(0);
  countSetRemote(0);
}

void loopCount() {
  // nothing to do
}

void countAddLocal(count_t delta) {
  countSetLocal(countLocal + delta);
}

void countSetLocal(count_t local) {
  if (countLocal != local) {
    countLocal = local;
    countLocalChangedAt = millis();

    Serial.print("countSetLocal: ");
    Serial.println(countLocal);    
  }
}

void countSetRemote(count_t remote) {
  if (countRemote != remote) {
    countRemote = remote;
    countRemoteChangedAt = millis();

    Serial.print("countSetRemote: ");
    Serial.println(countRemote);
  }
}

// ########################################## //
//                  BACKUP                    //
//  save local count in case of power outage  //
// ########################################## //

count_t backupLatestCount;
backup_id_t backupLatestId;
backup_index_t backupLatestIndex;
backup_index_t backupMaximumIndex;

void setupBackup() {
  backupLatestCount = 0;
  backupLatestId = 0;
  backupLatestIndex = 0;
  backupMaximumIndex = EEPROM.length() / sizeof(backup_fields) - 1;

  // find latest valid backup
  Serial.println("setupBackup: searching...");
  for (backup_index_t i = 0; i <= backupMaximumIndex; i++) {
    backup_fields fields = backupGet(i);
    if (backupIsValid(fields) && fields.id > backupLatestId) {
      Serial.print("setupBackup - found valid index ");
      Serial.print(i);
      Serial.print(" » id ");
      Serial.print(fields.id);
      Serial.print(" » count ");
      Serial.println(fields.count);

      backupLatestId    = fields.id;
      backupLatestCount = fields.count;
      backupLatestIndex = i;
    }
  }
  countSetLocal(backupLatestCount);
}

void loopBackup() {
  if (backupLatestCount == countLocal) return;
  if (millis() < countLocalChangedAt + BACKUP_DEBOUNCE_MS) return;

  backupLatestId++;
  backupLatestIndex = backupNextIndex();
  backupLatestCount = countLocal;

  backup_fields fields;
  fields.id = backupLatestId;
  fields.count = backupLatestCount;
  fields.checksum = backupChecksum(fields);
  backupSet(backupLatestIndex, fields);
}

backup_fields backupGet(backup_index_t index) {
  backup_fields fields;
  return EEPROM.get(backupAddress(index), fields);
}

backup_fields backupSet(uint16_t index, backup_fields fields) {
  Serial.print("backupSet ");
  Serial.print(index);
  Serial.print(" to count: ");
  Serial.println(fields.count);
  return EEPROM.put(backupAddress(index), fields);
}

backup_address_t backupAddress(backup_index_t index) {
  return index * sizeof(backup_fields);
}

backup_checksum_t backupChecksum(backup_fields fields) {
  uint8_t checksum = 0x5A;
  checksum ^= (uint8_t)(fields.id);
  checksum ^= (uint8_t)(fields.id >> 8);
  checksum ^= (uint8_t)(fields.id >> 16);
  checksum ^= (uint8_t)(fields.id >> 24);
  checksum ^= (uint8_t)(fields.count);
  checksum ^= (uint8_t)(fields.count >> 8);
  return checksum;
}

backup_index_t backupNextIndex() {
  return (backupLatestIndex >= backupMaximumIndex) ? 0 : backupLatestIndex + 1;
}

bool backupIsValid(backup_fields fields) {
  return fields.checksum == backupChecksum(fields);
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
    countAddLocal(+1);
  if (btnDecrease.onPressed())
    countAddLocal(-1);
}


// ########################################## //
//                 CONNECTION                 //
//            between the counters            //
// ########################################## //

SoftwareSerial connection(COMMUNICATION_RO, COMMUNICATION_DI);

void setupConnection() {
  pinMode(COMMUNICATION_DE, OUTPUT);
  pinMode(COMMUNICATION_RE, OUTPUT);
  setSending(false);

  connection.begin(9600);
}

void setSending(bool transmit) {
  digitalWrite(COMMUNICATION_DE, transmit ? HIGH : LOW);
  digitalWrite(COMMUNICATION_RE, transmit ? HIGH : LOW);
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
  connection.print(countLocal);
  connection.print("|");
  connection.flush();
  setSending(false);

  Serial.print("sendLocalCount: ");
  Serial.println(countLocal);
}

void receiveRemoteCount() {
  count_t remote = connection.parseInt();
  connection.read(); // for "|"
  countSetRemote(remote);

  Serial.print("receiveRemoteCount: ");
  Serial.println(countLocal);
}

// ########################################## //
//                  DISPLAY                   //
//         4-digit 7-segment-display          //
// ########################################## //

TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);
count_t displayNumber = 0;

void setupDisplay() {
  display.setBrightness(1);
  display.showNumberDec(0, DISPLAY_LEADING_ZEROS);
}

void loopDisplay() {
  count_t count = countTotal();
  if (count == displayNumber) return;
  
  display.showNumberDec(count, DISPLAY_LEADING_ZEROS);
  displayNumber = count;
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