#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

#define SENSOR_PIN 2
#define FAN_PWM_PIN 3
#define EEPROM_MAGIC_NUMBER 0xBEAF
#define PROM_START_ADDR

enum ProjectorState {
  STATE_ACTIVE,
  STATE_THERMAL_SHUTDOWN
};

struct SystemLog {
  unsigned int magic;
  float lifetimeMaxTemp;
  unsigned long lifetimeSeconds;
};

const float minTemp = 30.0;
const float maxTemp = 55.0;

float dynamicMinTemp = 30.0;
float dynamicMaxTemp = 55.0;
const float CRITICAL_MAX_TEMP = 65.0;

float filteredTemp = -127.0;
const float FILTER_ALPHA = 0.2;

ProjectorState currentSystemState = STATE_ACTIVE;
SystemLog diagnosticLog;
unsigned long lastSampleTime = 0;
unsigned long lastHourLogTime = 0;

OneWire oneWireObject(SENSOR_PIN);
DallasTemperature projectorSensors(&oneWireObject);

void setup() {
  Serial.begin(9600);
  projectorSensors.begin();
  pinMode(FAN_PWM_PIN, OUTPUT);

  TCCR2A = _BV(COM2B1) | _BV(WGM20);
  TCCR2B = _BV(WGM22) | _BV(CS21);
  OCR2A = 40;
  OCR2B = 0;

  loadSystemConfiguration();
  
  projectorSensors.requestTemperatures();
  float initialRead = projectorSensors.getTempCByIndex(0);
  if (initialRead != DEVICE_DISCONNECTED_C) {
    filteredTemp = initialRead;
  }
}

void loop() {
  unsigned long currentMillis = millis();

  processSerialInterface();

  if (currentMillis - lastSampleTime >= 1000) {
    lastSampleTime = currentMillis;

    projectorSensors.requestTemperatures();
    float rawTemp = projectorSensors.getTempCByIndex(0);

    if (rawTemp == DEVICE_DISCONNECTED_C) {
      Serial.println("Error: Temp sensor disconnected! Running fan at 100% speed.");
    setFanSpeedPercent(100);
    delay(1000);
    return;
    }

    if (filteredTemp == -127.0) {
      filteredTemp = rawTemp;
    } else {
      filteredTemp = (FILTER_ALPHA*rawTemp) + (1.0-FILTER_ALPHA)*filteredTemp;
    }

    if (filteredTemp > diagnosticLog.lifetimeMaxTemp) {
      diagnosticLog.lifetimeMaxTemp = filteredTemp;
      saveSystemConfiguration();
    }

    diagnosticLog.lifetimeSeconds += 1;
    if (currentMillis - lastHourLogTime >= 60000) {
      lastHourLogTime = currentMillis;
      saveSystemConfiguration();
    }

    if (filteredTemp >= CRITICAL_MAX_TEMP) {
      currentSystemState = STATE_THERMAL_SHUTDOWN;
    }

    float fanPercent = 0.0;

    switch(currentSystemState) {
      case STATE_ACTIVE:
        if (filteredTemp <= dynamicMinTemp) {
          fanPercent = 15.0;
        } else if (filteredTemp >= dynamicMaxTemp) {
          fanPercent = 100.0;
        } else {
          fanPercent=15.0+85.0*((filteredTemp-dynamicMinTemp)/(dynamicMaxTemp-dynamicMinTemp));
        }
        setFanSpeedPercent(fanPercent);
        break;
      case STATE_THERMAL_SHUTDOWN:
        setFanSpeedPercent(100);
        Serial.print("PEAK TEMP! EMERGENCY SHUTDOWN");
        Serial.print(filteredTemp,1);
        break;
    }
  }

  Serial.print("Projector temp: ");
  Serial.print(rawTemp, 1);
  Serial.print(" °C | Smooth: ");
  Serial.print(filteredTemp, 1);
  Serial.print(" °C | Fan: ");
  Serial.print(fanPercent, 1);


}

void setFanSpeedPercent(float percent) {
  if (percent<0) {
    percent = 0;
  }
  if (percent> 100) {
    percent = 100;
  }

  int registerValue = (percent/100.0)*40;
  OCR2B = registerValue;
}
void processSerialInterface() {
  if (Serial.available() >0 ) {
    String commandInput = Serial.readStringUntil('\n');
    commandInput.trim();
    commandInput.toUpperCase();

    if (commandInput.equals("HELP")) {
      Serial.println("STATUS - View operation measurements");
      Serial.println("SETMIN [xx] - Change minimum temperature target");
      Serial.println("SETMAX [xx] - Change maximum temperature target");
      Serial.println("RESETLOG - Clear lifetime memory stats");
    } else if (commandInput.equals("STATUS")) {
      Serial.print(F("  Current Live Temperature: ")); Serial.print(filteredTemp, 2); Serial.println(F(" °C"));
      Serial.print(F("  Software Low Limit Target:   ")); Serial.print(dynamicMinTemp, 1); Serial.println(F(" °C"));
      Serial.print(F("  Software High Limit Target:  ")); Serial.print(dynamicMaxTemp, 1); Serial.println(F(" °C"));
      Serial.print(F("  Historical Peak Absolute: ")); Serial.print(diagnosticLog.lifetimeMaxTemp, 2); Serial.println(F(" °C"));
      Serial.print(F("  Total Recorded Run Duration: ")); Serial.print(diagnosticLog.lifetimeSeconds / 3600.0, 2); Serial.println(F(" hours"));
    } else if (commandInput.startsWith("SETMIN ")) {
      String ValuePart = commandInput.substring(7);
      float proposedMin = valuePart.toFloat();
      if (proposedMin >=10.0 && proposedMin < dynamicMaxTemp) {
        dynamicMinTemp = proposedMin;
      } else {
        Serial.println("Attempted value out of bounds.");
      }
    } else if (commandInput.startsWith("SETMAX ")) {
      String ValuePart = commandInput.substring(7);
      float proposedMax = ValuePart.toFloat();
      if (proposedMax > dynamicMinTemp && proposedMax < CRITICAL_MAX_TEMP) {
        dynamicMaxTemp = proposedMax;
      } else {
        Serial.println("Attempted value out of bounds.");
      }
    } else if (commandInput.equals("RESETLOG")) {
      diagnosticLog.magic = EEPROM_MAGIC_NUMBER;
      diagnosticLog.lifetimeMaxTemp = 0.0;
      diagnosticLog.lifetimeSeconds = 0;
      SaveSystemConfiguration();
    } else {
      Serial.println("Commands unrecognized. Enter 'HELP' for a list of commands.")
    }
  }
}

void loadSystemConfiguration() {
  EEPROM.get(EEPROM_START_ADDR, diagnosticLog);
  if (diagnosticLog.magic != EEPROM_MAGIC_NUMBER) {
    diagnosticLog.magic = EEPROM_MAGIC_NUMBER;
    diagnosticLog.lifetimeMaxTemp = 0.0;
    diagnosticLog.lifetimeSeconds = 0;
    saveSystemConfiguration();
  }
}

void saveSystemConfiguration() {
  EEPROM.put(EEPROM_START_ADDR, diagnosticLog);
}