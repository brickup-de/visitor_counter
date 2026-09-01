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
#define BACKUP_MAXIMUM_AGE_MS 30000
#define BUTTON_DEBOUNCE_MS 20
#define BUTTON_PRESS_COOLDOWN_MS 3000
#define BUTTON_RESET_HOLD_MS 3000
#define BUTTON_RESET_WINDOW_MS 10000
#define DEBUG_BAUD_RATE 9600
#define DEBUG_DEBOUNCE_MS 1000
#define DISPLAY_LEADING_ZEROS false
#define LINK_BAUD_RATE 9600
#define LINK_BROKEN_AFTER_MS 10000
#define LINK_MESSAGE_BEGIN '['
#define LINK_MESSAGE_END ']'
#define LINK_SENT_DEBOUNCE_MS 2000
#define SOUND_CONFIRM_DURATION_MS 400
#define SOUND_CONFIRM_FREQUENCY_HZ 1500
#define SOUND_WARNING_DURATION_MS 200
#define SOUND_WARNING_FREQUENCY_HZ 1000
#define SOUND_WARNING_INTERVAL_MS 30000

// ########################################## //
//        TYPES, STRUCTURES, PROTOTYPES       //
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
count_t countReceived;
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
  // start from a known state, setupBackup is next
  countReset();
}

void loopCount() {
  // hold back changes while user is pressing buttons
  if (countReceived == countRemote)
    return;
  if (buttonPressedRecently())
    return;

  countSetRemote(countReceived);
}

// zero the remote counts as well, so that the display confirms
// the reset instead of waiting for the other counter to catch up
void countReset() {
  countSetLocal(0);
  countSetReceived(0);
  countSetRemote(0);
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

void countSetReceived(count_t received) {
  countReceived = received;
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
time_ms_t backupWrittenAt;
backup_id_t backupId;
backup_index_t backupIndex;
backup_index_t backupIndexMaximum;

void setupBackup() {
  backupCount = 0;
  backupWrittenAt = 0;
  backupId = 0;
  backupIndex = 0;
  
  backupIndexMaximum = EEPROM.length() / sizeof(backup_fields) - 1;
  debugValue("maxBackups", backupIndexMaximum + 1, true);

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

  if (backupId > 0) {
    soundConfirm();
  }
}

void loopBackup() {
  if (backupCount == countLocal)
    return;
  
  // wait for a stable count, but never postpone a backup for too long
  if ((timeSince(countLocalChangedAt) < BACKUP_DEBOUNCE_MS) 
   && (timeSince(backupWrittenAt) < BACKUP_MAXIMUM_AGE_MS))
    return;

  backupId++;
  backupIndex = backupNextIndex();
  backupCount = countLocal;

  backup_fields fields;
  fields.id = backupId;
  fields.count = backupCount;
  fields.checksum = backupChecksum(fields);
  backupSet(backupIndex, fields);
  backupWrittenAt = now;
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
time_ms_t buttonPressedAt;

void setupButtons() {
  pinMode(BUTTON_INCREASE_LED, OUTPUT);
  pinMode(BUTTON_DECREASE_LED, OUTPUT);
  digitalWrite(BUTTON_INCREASE_LED, LOW);
  digitalWrite(BUTTON_DECREASE_LED, LOW);
}

void loopButtons() {
  buttonIncrease.refreshStatus();
  buttonDecrease.refreshStatus();

  // button states are inverted in INPUT_PULLUP mode
  // (not handled by AcksenButton library)
  if (!buttonIncrease.getButtonState() || !buttonDecrease.getButtonState())
    buttonPressedAt = now;
  if (buttonIncrease.onReleased())
    countAddLocal(+1);
  if (buttonDecrease.onReleased())
    countAddLocal(-1);
  if (buttonResetTriggered()) {
    countReset();
    soundConfirm();
  }

  // the LEDs double as indicator lights
  digitalWrite(BUTTON_INCREASE_LED, buttonResetWindowOpen() ? HIGH : LOW);
  digitalWrite(BUTTON_DECREASE_LED, linkIsBroken() ? HIGH : LOW);
}

bool buttonPressedRecently() {
  return timeSince(buttonPressedAt) < BUTTON_PRESS_COOLDOWN_MS;
}

// reset only possible after power-on, so it doesn't happen by accident
bool buttonResetWindowOpen() {
  return now < BUTTON_RESET_WINDOW_MS;
}

bool buttonResetTriggered() {
  if (!buttonResetWindowOpen())
    return false;

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
time_ms_t linkReceivedAt;

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

bool linkIsBroken() {
  return timeSince(linkReceivedAt) >= LINK_BROKEN_AFTER_MS;
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

  linkReceivedAt = now;
  countSetReceived(remote);
}

// ########################################## //
//                  DISPLAY                   //
//         4-digit 7-segment-display          //
// ########################################## //

TM1637Display display(DISPLAY_CLK, DISPLAY_DIO);
count_t displayCount = 0;

void setupDisplay() {
  display.setBrightness(1);
  display.showNumberDec(0, DISPLAY_LEADING_ZEROS);
}

void loopDisplay() {
  count_t count = countTotal();
  if (count == displayCount)
    return;

  display.showNumberDec(count, DISPLAY_LEADING_ZEROS);
  displayCount = count;
}

// ########################################## //
//                    SOUND                   //
//   passive buzzer to produce beep sounds    //
// ########################################## //

time_ms_t soundWarnedAt;

void setupSound() {
  pinMode(SOUND_BUZZER, OUTPUT);
}

void soundConfirm() {
  tone(SOUND_BUZZER, SOUND_CONFIRM_FREQUENCY_HZ, SOUND_CONFIRM_DURATION_MS);
}

void loopSound() {
  if (!linkIsBroken()) {
    return;
  }
  if (timeSince(soundWarnedAt) < SOUND_WARNING_INTERVAL_MS)
    return;

  // tone() with a duration returns immediately
  tone(SOUND_BUZZER, SOUND_WARNING_FREQUENCY_HZ, SOUND_WARNING_DURATION_MS);
  soundWarnedAt = now;
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
  debugValue("sent", linkSentCount);
  debugValue("received", countReceived);
  debugValue("remote", countRemote);
  debugValue("display", displayCount);
  debugValue("backup", backupCount);
  debugValue("bIndex", backupIndex);
  debugValue("bId", backupId);
  debugValue("bAge", timeSince(backupWrittenAt));
  debugValue("linkAge", timeSince(linkReceivedAt));
  debugValue("bothBtnTime", buttonsBothHeldSince == 0 ? 0 : timeSince(buttonsBothHeldSince), true);
  debugOutputAt = now;
}

void debugValue(const char *key, int32_t value, bool endLine) {
  Serial.print(key);
  Serial.print('=');
  Serial.print(value);
  endLine ? Serial.println() : Serial.print('|');
}