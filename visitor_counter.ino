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
#define LINK_RS485_DI 8
#define LINK_RS485_DE 9
#define LINK_RS485_RE 10
#define LINK_RS485_RO 11
#define DISPLAY_CLK A5
#define DISPLAY_DIO A4
#define SOUND_BUZZER 6

// ########################################## //
//                   CONFIG                   //
//  adjustable values inside of the program   //
// ########################################## //

#define BACKUP_DEBOUNCE_MS 3000
#define BUTTON_DEBOUNCE_MS 20
#define BUTTON_RESET_HOLD_MS 5000
#define DEBUG_BAUD_RATE 9600
#define DEBUG_DEBOUNCE_MS 1000
#define DISPLAY_LEADING_ZEROS false
#define LINK_BAUD_RATE 9600
#define LINK_MESSAGE_BEGIN '['
#define LINK_MESSAGE_END ']'
#define LINK_SENT_DEBOUNCE_MS 2000

// ########################################## //
//            TYPES & STRUCTURES              //
//      must be defined before the coding     //
// ########################################## //

typedef uint8_t backup_checksum_t;
typedef uint32_t backup_id_t;
typedef uint16_t backup_index_t;
typedef int16_t count_t;
typedef uint16_t backup_address_t;
typedef unsigned long time_ms_t;

struct backup_fields {
  backup_id_t id;
  backup_checksum_t checksum;
  count_t count;
};

// functions with optional parameters must declare the
// default values in a prototype before the first usage.
void debugValue(const char *key, int32_t value, bool endLine = false);

// ########################################## //
//                SETUP & LOOP                //
// ########################################## //

void setup() {
  setupDebug();

  setupButtons();
  setupLink();
  setupDisplay();
  setupSound();

  setupCount();
  setupBackup();
}

void loop() {
  loopTime();

  loopBackup();
  loopButtons();
  loopLink();
  loopCount();
  loopDisplay();
  loopSound();

  loopDebug();
}

// ########################################## //
//                    TIME                    //
//  helpers to avoid millis-overflow trouble  //
// ########################################## //
time_ms_t now = 0;

void loopTime() {
  now = millis();
}

unsigned long timeSince(time_ms_t event) {
  return now - event;
}

// ########################################## //
//                   COUNT                    //
// local/remote/total count; can be negative  //
// ########################################## //

count_t countLocal;
count_t countRemote;
count_t countTotal() {
  return countLocal + countRemote;
}

time_ms_t countLocalChangedAt;
time_ms_t countRemoteChangedAt;
time_ms_t countTotalChangedAt() {
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
    countLocalChangedAt = now;
  }
}

void countSetRemote(count_t remote) {
  if (countRemote != remote) {
    countRemote = remote;
    countRemoteChangedAt = now;
  }
}

// ########################################## //
//                  BACKUP                    //
//  save local count in case of power outage  //
// ########################################## //

count_t backupCount;
backup_id_t backupId;
backup_index_t backupIndex;
backup_index_t backupIndexMaximum;

void setupBackup() {
  backupCount = 0;
  backupId = 0;
  backupIndex = 0;
  backupIndexMaximum = EEPROM.length() / sizeof(backup_fields) - 1;

  // find latest valid backup
  Serial.println("setupBackup: searching...");
  for (backup_index_t i = 0; i <= backupIndexMaximum; i++) {
    backup_fields fields = backupGet(i);
    if (backupIsValid(fields) && fields.id > backupId) {
      debugValue("foundBackupIndex", i);
      debugValue("id", fields.id);
      debugValue("count", fields.count, true);

      backupId = fields.id;
      backupCount = fields.count;
      backupIndex = i;
    }
  }
  countSetLocal(backupCount);
}

void loopBackup() {
  if (backupCount == countLocal)
    return;
  if (timeSince(countLocalChangedAt) < BACKUP_DEBOUNCE_MS)
    return;

  backupId++;
  backupIndex = backupNextIndex();
  backupCount = countLocal;

  backup_fields fields;
  fields.id = backupId;
  fields.count = backupCount;
  fields.checksum = backupChecksum(fields);
  backupSet(backupIndex, fields);
}

backup_fields backupGet(backup_index_t index) {
  backup_fields fields;
  return EEPROM.get(backupAddress(index), fields);
}

backup_fields backupSet(uint16_t index, backup_fields fields) {
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
  return (backupIndex >= backupIndexMaximum) ? 0 : backupIndex + 1;
}

bool backupIsValid(backup_fields fields) {
  return fields.checksum == backupChecksum(fields);
}

// ########################################## //
//                  BUTTONS                   //
//      Arcade switches with an LED each      //
// ########################################## //

AcksenButton buttonIncrease(BUTTON_INCREASE, ACKSEN_BUTTON_MODE_NORMAL, BUTTON_DEBOUNCE_MS, INPUT_PULLUP);
AcksenButton buttonDecrease(BUTTON_DECREASE, ACKSEN_BUTTON_MODE_NORMAL, BUTTON_DEBOUNCE_MS, INPUT_PULLUP);
time_ms_t buttonsBothHeldSince;

void setupButtons() {
  pinMode(BUTTON_INCREASE_LED, OUTPUT);
  pinMode(BUTTON_DECREASE_LED, OUTPUT);
  digitalWrite(BUTTON_INCREASE_LED, LOW);
  digitalWrite(BUTTON_DECREASE_LED, LOW);
}

void loopButtons() {
  buttonIncrease.refreshStatus();
  buttonDecrease.refreshStatus();

  // onReleased = onPressed, because of INPUT_PULLUP mode 
  // LOW and HIGH state are inverted (not handled by AcksenButton library)
  if (buttonIncrease.onReleased())
    countAddLocal(+1);
  if (buttonDecrease.onReleased())
    countAddLocal(-1);
  if (buttonResetTriggered())
    countSetLocal(0);
}

bool buttonResetTriggered() {
  if (buttonIncrease.getButtonState() || buttonDecrease.getButtonState()) {
    buttonsBothHeldSince = 0;
    return false;
  }

  if (buttonsBothHeldSince == 0) {
    buttonsBothHeldSince = now;
    return false;
  }

  if (timeSince(buttonsBothHeldSince) > BUTTON_RESET_HOLD_MS) {
    buttonsBothHeldSince = 0;
    return true;
  }

  return false;
}

// ########################################## //
//                    LINK                    //
//    RS485 connection between the counters   //
// ########################################## //

SoftwareSerial link(LINK_RS485_RO, LINK_RS485_DI);
count_t linkSentCount;
time_ms_t linkSentAt;

void setupLink() {
  pinMode(LINK_RS485_DE, OUTPUT);
  pinMode(LINK_RS485_RE, OUTPUT);
  linkSetSending(false);

  link.begin(LINK_BAUD_RATE);
  link.setTimeout(50);
}

void loopLink() {
  if (link.available() > 0) {
    receiveRemoteCount();
  } else {
    linkSendLocalCount();
  }
}

void linkSetSending(bool transmit) {
  digitalWrite(LINK_RS485_DE, transmit ? HIGH : LOW);
  digitalWrite(LINK_RS485_RE, transmit ? HIGH : LOW);
}

void linkSendLocalCount() {
  if ((linkSentCount == countLocal) && (timeSince(linkSentAt) < LINK_SENT_DEBOUNCE_MS))
    return;

  linkSetSending(true);
  link.print(LINK_MESSAGE_BEGIN);
  link.print(countLocal);
  link.print(LINK_MESSAGE_END);
  link.flush();
  linkSetSending(false);

  linkSentCount = countLocal;
  linkSentAt = now;
}

void receiveRemoteCount() {
  char begin = link.read();
  if (begin != LINK_MESSAGE_BEGIN)
    return;

  count_t remote = link.parseInt();
  char end = link.read();
  if (end != LINK_MESSAGE_END)
    return;

  countSetRemote(remote);
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
  if (count == displayNumber)
    return;

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

// ########################################## //
//                    DEBUG                   //
//     send important values over Serial      //
// ########################################## //
time_ms_t debugOutputAt;

void setupDebug() {
  Serial.begin(DEBUG_BAUD_RATE);
}

void loopDebug() {
  if (timeSince(debugOutputAt) < DEBUG_DEBOUNCE_MS)
    return;

  debugValue("local", countLocal);
  debugValue("backup", backupCount);
  debugValue("sent", linkSentCount);
  debugValue("remote", countRemote);
  debugValue("backupIndex", backupIndex);
  debugValue("backupId", backupId, true);
  debugOutputAt = now;
}

void debugValue(const char *key, int32_t value, bool endLine) {
  Serial.print(key);
  Serial.print(',');
  Serial.print(value);
  endLine ? Serial.println() : Serial.print(',');
}