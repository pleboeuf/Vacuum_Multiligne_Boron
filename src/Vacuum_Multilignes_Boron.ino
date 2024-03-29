/*
 * Project Vacuum_Multilignes_Electron
 * Description: Code running on an Electron on the "Capteur de vide multilignes"
 * Author: Pierre Leboeuf
 * Date: 30 oct 2018
 */

/*
*** Notes about sleep ***
Sleep mode: Stop mode + SLEEP_NETWORK_STANDBY, use SETUP BUTTON (20) to wake-up
Publish: NO_ACK
Delay loop: 500 ms for publish and print
Sleep duration: See #define SLEEPTIMEinMINUTES
*/

// *** Commande pour tracer sur le Raspberry pi
// screen -S DEV1 -L dev1_auto_inside_080rc11 /dev/ttyUSB0 115200

// Dev name      No de lignes
//  VA1-4     4  Lignes A1 à A4 RSSI = -77, qual 37
//  VA5B1-2   3  Lignes A5, B1 et B2
//  VC1-3     3  Lignes C1 à C3
//  VC4-6     3  Lignes C4 à C6
//  VC7-8     2  Lignes C7 et C8
//  VD1A-2B   4  Lignes D1A, D1B, D2A et D2B
//  VE1-3     3  Lignes E1 à E3
//  VE4-6     3  Lignes E4 à E6
//  VE7-9     3  Lignes E7 à E9
//  VE10-12   3  Lignes E10 à E12
//  VF1-3     3  Lignes F1 à F3
//  VF4-6     3  Lignes F4 à F6
//  VF7-9     3  Lignes F7 à F9 RSSI = -81, qual 37
//  VF10-12   3  Lignes F10 à F12
//  VF13-16   4  Lignes F13 à F16
//  VG1-2-H14 3  Lignes G1, G2 et H14
//  VG3-5     3  Lignes G3 à G5
//  VG6-8     3  Lignes G6 à G8
//  VG9-12    4  Lignes G9 à G12
//  VH2-4     3  Lignes H2 à H4
//  VH5-7     3  Lignes H5 à H7
//  VH8-10    3  Lignes H8 à H10
//  VH11-13   3  Lignes H11 à H13 RSSI = -91, qual 19

// Date de changement d'heure au Québec: 2ième dimanche de mars (10 mars 2019) +1h.
// Premier dimanche de novembre (3 novembre 2019) -1h.

#include "Particle.h"
#include "math.h"
#include "photon-thermistor.h"

SYSTEM_THREAD(ENABLED);
SYSTEM_MODE(SEMI_AUTOMATIC);

// General definitions
String FirmwareVersion = "2.0.0"; // Version of this firmware.
String thisDevice = "";
String F_Date = __DATE__;
String F_Time = __TIME__;
String FirmwareDate = F_Date + " " + F_Time; // compilation date and time (UTC)
String myEventName = "Vacuum/Lignes";        // Name of the event to be sent to the cloud
String myNameIs = "";
String myID = ""; // Device Id

#define SLEEPTIMEinMINUTES 5     // wake-up every SLEEPTIMEinMINUTES and check if there a reason to publish
#define ONEHOURSLEEPTIMEinMIN 60 // 60 MINUTES
#define ONEHOURinSECONDS 3600UL
#define MINUTES 60UL //

#define WakeUpCountToPublish 3 // Number of wake-up before publishing
#define unJourEnMillis (24 * 60 * 60 * 1000)
#define NIGHT_SLEEP_START_HR 21           // Sleep for the night beginning at 21h00
#define NIGHT_SLEEP_START_MIN 00          // Sleep for the night beginning at 21h00
#define AUTO_CALIB_START_HR 02            // Hours at which time to start the auto calibration of external temperature
#define AUTO_CALIB_START_MIN 00           // minute at which time to start the auto calibration of external temperature
#define NIGHT_SLEEP_LENGTH_HR 10          // Night sleep duration in hours
#define TOO_COLD_SLEEP_IN_SECONDs 10 * 60 //
#define TimeBoundaryOffset 0              // wake-up at time boundary plus some seconds
// #define WatchDogTimeout 480000UL          // Watch Dog Timeaout delay

#define NUMSAMPLES 5                    // Number of readings to average to reduce the noise
#define SAMPLEsINTERVAL 20UL            // Interval of time between samples in ms. A value of 20ms is optimal in my tests
#define VacuumPublishLimits -1          // Minimum vacuum required to permit publish( 1 always publish, -1: publish only if vacuum) NOT USED
#define VacMinChange 1                  // Minimum changes in vacuum to initiate a publish within SLEEPTIMEinMINUTES
#define StartDSTtime 1678604400         // dim 12 mar 2023, 02 h 00 = 1678604400 local time
#define EndDSTtime 1699167600           // dim 05 nov 2023, 02 h 00 = 1699167600 local time
#define ChargeVoltageMaxLevel_cold 4208 // Max voltage allow by charger for cold period
#define ChargeVoltageMaxLevel_warm 4112 // Max voltage allow by charger for warm period to prevent overcharge
#define UpdateCounterInit 4             // Number of cycle to stay awake for software update and calibration

#define BLUE_LED D7 // Blue led awake activity indicator

// wakeupPin definition
#define wakeupPin D4

// Temperature and thermistor parameters and variables definitions
#define TEMPERATURENOMINAL 25      // Ref temperature for thermistor
#define BCOEFFICIENT 3470          // The beta coefficient at 0 degrees of the thermistor (nominal is 3435 (25/85))
#define SERIESRESISTOR 10000UL     // the value of the resistor in serie with the thermistor
#define THERMISTORNOMINAL 10000UL  // thermistor resistance at 25 degrees C
#define validCalibAddress 0        // Adresse of flag indicating that thermistor offset is calibrated
#define ThermistorOffsetAddress 10 // Adresse of thermistor offset value in EEPROM
#define ThermistorSlopeAddress 20  // Adresse of thermistor slope value in EEPROM

int currentYear = 2023;
float minBatteryLevel = 20.0; // Sleep unless battery is above this level
const std::chrono::milliseconds maxConnectTime = 6min;

Thermistor *ext_thermistor;
// Thermistor *battery_thermistor;
int thermistorPowerPin = D3;
int Ext_thermistorInputPin = A1;
// int Bat_thermistorInputPin = B5;
// int VinPin = A6;

float ExtTemp = 0.0;
float BatteryTemp = 0.0;
float minPublishTemp = -3.0;   // Do not publish below -2.5
float lowTempLimit = -15.0;    // Reduce the wake-up period from 5 min to 1 hour below this temperature
double thermistorOffset = 0.0; // 2.8
double thermistorSlope = 1.0;  // 1.0
uint32_t thermIsCalibrated = 0;
bool enableAutoCalib = false;

// Sleep and charger variables definitions
enum sleepTypeDef
{
    SLEEP_NORMAL,
    SLEEP_TOO_COLD,
    SLEEP_LOW_BATTERY,
    SLEEP_VERY_COLD,
    SLEEP_All_NIGHT,
    SLEEP_5_MINUTES,
    NO_SLEEP
};
enum chState
{
    off,
    lowCurrent,
    highCurrent,
    unknown
};
chState chargerStatus = unknown;
bool NightSleepFlag = false;

// Light sensor parameters and variable definitions
#define LOADRESISTOR 51000UL   // Resistor used to convert current to voltage on the light sensor
int lightSensorEnablePin = D2; // Active LOW
int lightSensorInputPin = A0;
float lightIntensityLux = 0;

// Vacuum transducer parameters and variable definitions
#define R1 16900UL                     // pressure xducer scaling resistor 1
#define R2 32400UL                     // Pressure xducer scaling resistor 2
#define Vref 3.3                       // Analog input reference voltage
#define K_fact 0.007652                // Vacuum xducer K factor
#define Vs 5.0                         // Vacuum xducer supply voltage
#define Vcc 3.3                        // Analog system reference voltage
#define ResistorScaling (R1 + R2) / R2 // To scale output of 5.0V xducer to 3.3V Electron input

#define NVac 4                  // Number of vacuum sensors
int vacuum5VoltsEnablePin = D6; // Vacuums sensors operate at 5V
int VacuumPins[] = {A5, A4, A3, A2};
float minActualVacuum = 100;
static float VacuumInHg[] = {0, 0, 0, 0};      // Vacuum scaled data array
static float PrevVacuumInHg[] = {10, 0, 0, 0}; // Vacuum reading at the preceding iteration

// Cellular signal and data variables definitions
float signalRSSI;
float signalQuality;
int txPrec = 0;  // Previous tx data count
int rxPrec = 0;  // Previous rx data count
int deltaTx = 0; // Difference tx data count
int deltaRx = 0; // Difference rx data count
int startTime = 0;
int start = 0;
int wakeup_time = 0; // Wakeup time in ms
retained int lastDay = 0;
// int lastDay = 0;

// Various variable and definitions
retained int noSerie; // Le numéro de série est généré automatiquement
retained int newGenTimestamp = 0;
unsigned long stateTime;
// retained int restartCount = 0;
int WakeUpCount = 0;
retained int FailCount = 0;
retained int SigSystemResetCount = 0;

FuelGauge fuel;
float soc = 0;
float Vbat = 0;

unsigned long lastRTCSync = millis();
bool SoftUpdateDisponible = false;
int updateCounter = UpdateCounterInit;
unsigned long lastSync = millis();
char publishStr[120];

/* Define a log handler on Serial1 for log messages */
Serial1LogHandler log1Handler(9600, LOG_LEVEL_INFO, {
                                                        // Logging level for non-application messages
                                                        {"app", LOG_LEVEL_INFO} // Logging level for application messages
                                                    });

// ***************************************************************
/// @brief   Initialisation
// ***************************************************************
void setup()
{
    Cellular.on();
    Particle.connect();
    stateTime = millis();
    Log.info("\n\n__________BOOTING_________________");
    Log.info("(setup) Boot on: " + Time.timeStr(Time.now() - 5 * 60 * 60));
    Log.info("(setup) System version: " + System.version());
    Log.info("(setup) Firmware: " + FirmwareVersion);
    Log.info("(setup) Firmware date: " + FirmwareDate);
    Time.zone(-5);
    pinMode(vacuum5VoltsEnablePin, OUTPUT); // Put all control pins in output mode
    pinMode(lightSensorEnablePin, OUTPUT);
    pinMode(thermistorPowerPin, OUTPUT);
    pinMode(BLUE_LED, OUTPUT);
    pinMode(wakeupPin, INPUT_PULLUP);
    //
    // RGB.mirrorTo(D12, D11, D13, true); // Mirror LED on external LED
    //
    // pinMode(D5, INPUT);   // Only required for old PCB design
    ext_thermistor = new Thermistor(Ext_thermistorInputPin, SERIESRESISTOR, THERMISTORNOMINAL, TEMPERATURENOMINAL, BCOEFFICIENT, NUMSAMPLES, SAMPLEsINTERVAL);
    // battery_thermistor = new Thermistor(Bat_thermistorInputPin, SERIESRESISTOR, 4095, THERMISTORNOMINAL, TEMPERATURENOMINAL, BCOEFFICIENT, NUMSAMPLES, SAMPLEsINTERVAL);
    soc = fuel.getSoC();
    Vbat = fuel.getVCell();
    delay(5000UL);
    myID = System.deviceID();
    getDeviceEventName(myID);
    soc = fuel.getSoC();
    Vbat = fuel.getVCell();
    initSI7051();
    ExtTemp = readThermistor(1, 1, "Ext"); // First check the temperature
    // BatteryTemp = readThermistor(1, 1, "Bat");
    Log.info("(setup) Battery boot voltage: %0.3f ,charge level: %0.2f, and temperature: %0.1f", Vbat, soc, BatteryTemp);
    configCharger(true);
    lightIntensityLux = readLightIntensitySensor(); // Then read light intensity
    Particle.subscribe("UpdateAvailable", SoftUpdate_Handler, MY_DEVICES);
    Particle.function("setThermOffset", setThermistorOffset);
    Particle.function("setThermSlope", setThermistorSlope);
    Particle.function("setAutoCalib_Flag", setAutoCalibFlag);
    Particle.function("setNightSleep_Flag", setNightSleepFlag);
    Particle.function("setSoftUpdate_Flag", setSoftUpdateFlag);

    Particle.variable("Version", FirmwareVersion);
    Particle.variable("Therm_Cal_Status", thermIsCalibrated);
    Particle.variable("ThermistorOffset", thermistorOffset);
    Particle.variable("ThermistorSlope", thermistorSlope);
    Particle.variable("SoftUpdate_status", SoftUpdateDisponible);

    EEPROM.get(validCalibAddress, thermIsCalibrated);
    if (thermIsCalibrated != 0xFFFFFFFF)
    {
        EEPROM.get(ThermistorOffsetAddress, thermistorOffset);
        Log.info("(setup) Thermistor is calibrated: %f", thermistorOffset);
    }
    // connectTowerAndCloud();
    Particle.publishVitals(); // Pour tester la force du signal
    Log.info("(setup) Setup Completed");
}

// ***************************************************************
/// @brief   Managing the tower and cloud connexion
// ***************************************************************
void connectTowerAndCloud()
{
    if (soc > minBatteryLevel)
    {
        // Wait for the connection to the Particle cloud to complete
        if (Particle.connected())
        {
            Log.info("(connectTowerAndCloud) Cloud connected! - " + Time.timeStr());
            if (not(Time.isValid()))
            {
                Log.info("(connectTowerAndCloud) Syncing time ");
                Particle.syncTime();
                waitUntil(Particle.syncTimeDone);
                Log.info("(connectTowerAndCloud) syncTimeDone " + Time.timeStr());
            }
            int tryCnt = 0;
            while (not(Time.isValid()) && tryCnt < 10)
            {
                delay(2s);
                tryCnt++;
            }
            if (not(Time.isValid()))
            {
                Log.info("failed to connect, going to sleep");
                setSleepState(SLEEP_5_MINUTES);
            }
            newGenTimestamp = Time.now();
            SigSystemResetCount = 0;
            return;
        }
        else if (millis() - stateTime >= maxConnectTime.count())
        {
            // Took too long to connect, go to sleep and reset
            Log.info("(connectTowerAndCloud) Failed to connect, going to sleep:, millis:%lu, stateTime:%lu, maxConnectTime:%lld",
                     millis(), stateTime, maxConnectTime.count());
            setSleepState(SLEEP_5_MINUTES);
        }
    }
}

// ***************************************************************
/// @brief  Main Loop
// ***************************************************************
void loop()
{
    int sleepStyle = NO_SLEEP;
    bool vacChanged = false;
    // First check battery state, external temperature and battery temperature (through the configCharger routine)
    soc = fuel.getSoC();
    Vbat = fuel.getVCell();
    Log.info("(loop) Battery level %0.1f", soc);
    ExtTemp = readThermistor(1, 1, "Ext");
    configCharger(true);

    // Do not publish if charge is lower than minBatteryLevel (20%)
    if (soc < minBatteryLevel)
    {
        // SLEEP for an hour to recharge the battery
        sleepStyle = SLEEP_LOW_BATTERY; // duration = 1 hour
    }
    else if (ExtTemp < lowTempLimit)
    {
        // When temperature is very cold (below -15)
        sleepStyle = SLEEP_VERY_COLD; // duration =  1 hour
    }
    // Stop Publish when external temperature is below the minPublishTemp - 0.5°C then enter low power sleep mode
    else if (ExtTemp <= minPublishTemp - 0.5)
    {
        sleepStyle = SLEEP_TOO_COLD; // duration 5 minutes
    }
    // Start Publish when external temperature is above the minPublishTemp + 0.5°C then enter low power sleep mode
    else if (ExtTemp >= minPublishTemp + 0.5)
    {
        if (Particle.connected())
        {
            // Keep Publishing when close to minPublishTemp if the connection was already established
            vacChanged = readVacuums(); // Then VacuumPublishLimits
            if ((WakeUpCount == WakeUpCountToPublish) || vacChanged)
            {
                publishData(); // Publish the data
            }
            sleepStyle = SLEEP_NORMAL; // duration 5 minutes
        }
        else
        {
            // Keep the low power mode when close to minPublishTemp if the connection was already disconnected
            Particle.connect();
            connectTowerAndCloud();
        }
    }
    setSleepState(sleepStyle);
    delay(1s);
}

// ***************************************************************
/// @brief   Software update handler - A modifier
// ***************************************************************
void SoftUpdate_Handler(const char *event, const char *data)
{
    setSoftUpdateFlag("true");
}

// ***************************************************************
/// @brief   Initialisation of SI7051 temperature sensor - To move to a library
// ***************************************************************
void initSI7051()
{
    Log.info("init SI7051");
    Wire.begin();
    Wire.beginTransmission(0x40);
    Wire.write(0xE6);
    Wire.write(0x0);
    Wire.endTransmission();
    Log.info("init SI7051 complété");
}

// ***************************************************************
/// @brief Disable light intensity sensor and RGB LED mirroring
// ***************************************************************
void setRGBmirorring(bool state)
{
    if (state == true)
    {
        digitalWrite(lightSensorEnablePin, false); // Turn ON the light sensor
        RGB.mirrorTo(D12, D11, D13, true);         // Enable RGB LED mirroring
    }
    else
    {
        digitalWrite(lightSensorEnablePin, true);
        RGB.mirrorDisable();
    }
    Particle.process();
}

// ***************************************************************
/// @brief Determine sleep time and mode
// ***************************************************************
void setSleepState(int sleepType)
{
    if (sleepType == NO_SLEEP)
    {
        return;
    }
    // Log info for debugging before going to sleep
    lightIntensityLux = readLightIntensitySensor(); // Then read light intensity
    String timeNow = Time.format(Time.now(), TIME_FORMAT_ISO8601_FULL);
    Log.info("(setSleepState) Temp_Summary: Time, Li, Vbat, SOC, ExtTemp, SI7051 :\t" + timeNow +
                 "\t%.0f\t%.3f\t%.2f\t%.1f\t%.1f",
             lightIntensityLux, Vbat, soc, ExtTemp, readSI7051());

    if (enableAutoCalib == true && Time.hour() == AUTO_CALIB_START_HR && Time.minute() == AUTO_CALIB_START_MIN)
    {
        double deltaTemp = ExtTemp - readSI7051();
        // To prevent autocalib in case the SI7051 is bad.
        if (abs(deltaTemp) < 7.0)
        {
            thermistorOffset = thermistorOffset - deltaTemp;
            char newThermOffset[10] = "";
            sprintf(newThermOffset, "%.2f", thermistorOffset);
            Log.info("(setSleepState) New thermistor offset is: %s", newThermOffset);
            setThermistorOffset(newThermOffset);
            enableAutoCalib = false;
        }
    }

    switch (sleepType)
    {
    case SLEEP_NORMAL:
        Log.info("(setSleepState) 'SLEEP_NORMAL' ");
        normal_sleep();
        break;

    case SLEEP_TOO_COLD:
        Log.info("(setSleepState) Too cold to publish. ");
        normal_sleep();
        break;

    case SLEEP_5_MINUTES:
        Log.info("(setSleepState) Difficulty connecting to the cloud. ");
        fiveMinutes_sleep();
        break;

    case SLEEP_LOW_BATTERY:
        Log.info("(setSleepState) LOW_BATTERY ");
        oneHourSleep();
        break;

    case SLEEP_VERY_COLD:
        Log.info("(setSleepState) Much too cold! ");
        oneHourSleep();
        break;
    }

    // wake-up time
    wakeup_time = millis();
    stateTime = wakeup_time;
    setRGBmirorring(true); // Enable RGB LED mirroring
    WakeUpCount++;
    WakeUpCount = WakeUpCount % 4;
    Log.info(" ");
    Log.info("(setSleepState)  Wake-up on: " + Time.timeStr() + ", WakeUpCount= %d", WakeUpCount); // Log wake-up time
}

/// @brief Calcul le temps de sommeil nécessaire pour synchroniser les réveils avec un interval de temps donné. Exemple toute les 5 minutes
/// @param durationInMin Interal de temps en minutes
/// @return Le temps de sommeil nécessaire
unsigned long calcSleepDuration(unsigned long durationInMin)
{
    unsigned long sleepDuration;
    if (Time.isValid())
    {
        sleepDuration = ((durationInMin - Time.minute() % durationInMin) * 60 - Time.second() - TimeBoundaryOffset);
        return (min(sleepDuration, durationInMin * 60)); // In case of calculation error I had occasionnaly
    }
    else
    {
        Log.info("(calcSleepDuration) Time is not valid. Sleeping typical time");
        sleepDuration = 294;
        return (sleepDuration);
    }
}

/// @brief Normal sleep i.e. STOP Mode sleep
void normal_sleep()
{
    SystemSleepConfiguration config;
    unsigned long sleepTimeSecond = 0;
    // sleeps duration corrected to next time boundary + TimeBoundaryOffset seconds
    sleepTimeSecond = calcSleepDuration(SLEEPTIMEinMINUTES);
    if (SoftUpdateDisponible)
    {
        if (updateCounter-- <= 0)
        {
            SoftUpdateDisponible = false;
        }
        Log.info("(setSleepState) OTA available, NOT SLEEPING! updateCounter = %d", updateCounter);
        Log.info("(setSleepState) 'Waiting' for %lu seconds.", sleepTimeSecond);
        for (unsigned long i = 0; i < (sleepTimeSecond - 2); i++)
        {
            Particle.process(); // Gives time to system
            delay(1000UL);
        }
    }
    else
    {
        Log.info("(normal_sleep) 'SLEEPING for %lu seconds", sleepTimeSecond);
        config.mode(SystemSleepMode::STOP)
            .network(NETWORK_INTERFACE_CELLULAR, SystemSleepNetworkFlag::INACTIVE_STANDBY)
            .gpio(wakeupPin, FALLING)
            .flag(SystemSleepFlag::WAIT_CLOUD)
            .duration(1000 * sleepTimeSecond);
        delay(2000UL);
        setRGBmirorring(false);
        SystemSleepResult result = System.sleep(config);
        delay(2s);
        Log.info("(normal_sleep) 'Wake up reason: ", result.wakeupReason());
        if (result.wakeupReason() == SystemSleepWakeupReason::BY_GPIO)
        {
            Particle.publishVitals(); // Publier les vitals si réveillé par GPIO
            Log.info("(normal_sleep) 'Wake up by GPIO: Publishing Vitals!", result.wakeupReason());
        }
    }
}

/// @brief  Too cold to publish! Wait until its warmer - Long sleep
void tooCold_sleep()
{
    SystemSleepConfiguration config;
    unsigned long sleepTimeSecond = 0;

    sleepTimeSecond = calcSleepDuration(SLEEPTIMEinMINUTES);
    Log.info("(setSleepState) Too cold to publish. - 'SLEEP_TOO_COLD' for %lu seconds.", sleepTimeSecond);
    config.mode(SystemSleepMode::STOP)
        .network(NETWORK_INTERFACE_CELLULAR, SystemSleepNetworkFlag::INACTIVE_STANDBY)
        .gpio(wakeupPin, FALLING)
        .flag(SystemSleepFlag::WAIT_CLOUD)
        .duration(1000 * sleepTimeSecond);
    delay(2000UL);
    setRGBmirorring(false);
    System.sleep(config);
}

/// @brief   Low battery sleep i.e. STOP Mode sleep for 1 hour to gives time to battery to recharge
void lowBattery_sleep()
{
    SystemSleepConfiguration config;
    unsigned long sleepTimeSecond = 0;

    sleepTimeSecond = calcSleepDuration(ONEHOURSLEEPTIMEinMIN);
    Log.info("(setSleepState) Battery below %0.1f percent. 'SLEEP_LOW_BATTERY' for %lu seconds.", minBatteryLevel, sleepTimeSecond);
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)
        .flag(SystemSleepFlag::WAIT_CLOUD)
        .duration(1000 * sleepTimeSecond);
    delay(2000UL);
    setRGBmirorring(false);
    System.sleep(config);
}

/// @brief   Low battery sleep i.e. STOP Mode sleep for 1 hour to gives time to battery to recharge
void veryCold_sleep()
{
    SystemSleepConfiguration config;
    unsigned long sleepTimeSecond = 0;

    sleepTimeSecond = calcSleepDuration(ONEHOURSLEEPTIMEinMIN);
    Log.info("(setSleepState) Much too cold! - 'SLEEP_VERY_COLD' for %lu seconds to protect battery.", sleepTimeSecond);
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)
        .flag(SystemSleepFlag::WAIT_CLOUD)
        .duration(1000 * sleepTimeSecond);
    delay(2000UL);
    setRGBmirorring(false);
    System.sleep(config);
}

/// @brief   Low battery sleep i.e. STOP Mode sleep for 1 hour to gives time to battery to recharge
void oneHourSleep()
{
    SystemSleepConfiguration config;
    unsigned long sleepTimeSecond = 0;

    sleepTimeSecond = calcSleepDuration(ONEHOURSLEEPTIMEinMIN);
    // sleepTimeSecond = 60 * ONEHOURSLEEPTIMEinMIN - 6; // One hour minus 6 seconds
    Particle.disconnect();
    Log.info("(oneHourSleep) Much too cold! - 'SLEEPING for %lu seconds", sleepTimeSecond);
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)
        .flag(SystemSleepFlag::WAIT_CLOUD)
        .duration(1000 * sleepTimeSecond);
    delay(2000UL);
    setRGBmirorring(false);
    System.sleep(config);
}

/// @brief   Five minutes retry sleep
void fiveMinutes_sleep()
{
    SystemSleepConfiguration config;
    Log.info("(fiveMinutes_sleep) 'SLEEPING for %lu seconds", 300UL);
    config.mode(SystemSleepMode::ULTRA_LOW_POWER)
        .network(NETWORK_INTERFACE_CELLULAR, SystemSleepNetworkFlag::INACTIVE_STANDBY)
        .gpio(wakeupPin, FALLING)
        .duration(1000 * 5min);
    delay(2000UL);
    setRGBmirorring(false);
    System.sleep(config);
    System.reset();
}

// /// @brief   Sleep all night to save batteries
// void allNight_sleep()
// {
//     // SystemSleepConfiguration config;
//     // unsigned long sleepTimeSecond = 0;
//     // delay(2000UL);
//     // // sleepTimeSecond = (NightSleepTimeInMinutes - Time.minute() % NightSleepTimeInMinutes) * 60 - Time.second() + TimeBoundaryOffset;
//     // sleepTimeSecond = calcSleepDuration(NightSleepTimeInMinutes);
//     //
//     // Log.info("(setSleepState) It's %d O'clock. Going to 'SLEEP_All_NIGHT' for %lu minutes and %lu seconds.", Time.hour(), sleepTimeSecond / 60, (sleepTimeSecond / 60) % 60);
//     // // Disable the charger before night sleep
//     // configCharger(false);
//     // Log.info("(setSleepState) 'SLEEP_All_NIGHT' - Go to sleep at : %s for %lu seconds", timeNow.c_str(), sleepTimeSecond);
//     // if (sleepTimeSecond > NIGHT_SLEEP_LENGTH_HR * 60 * 60)
//     // {
//     //     sleepTimeSecond = NIGHT_SLEEP_LENGTH_HR * 60 * 60;
//     // }
//     // Log.info("(setSleepState) sleepTimeSecond = %lu", sleepTimeSecond);
//     // if (Particle.connected())
//     // {
//     //     Particle.disconnect();
//     //     Cellular.disconnect();
//     //     // Cellular.off();
//     // }
//     // delay(2000UL);
//     // setRGBmirorring(false);
//     // // config.mode(SystemSleepMode::STOP)
//     // //     .network()
//     // //     // .network(HAL_SLEEP_NETWORK_FLAG_INACTIVE_STANDBY)
//     // //     .gpio(wakeupPin, FALLING)
//     // //     .flag(SystemSleepFlag::WAIT_CLOUD)
//     // //     .duration(1000 * sleepTimeSecond);
//     // // System.sleep(config);
//     // System.sleep(wakeupPin, FALLING, sleepTimeSecond); // Low power mode
// }

// ***************************************************************
/// @brief Publish data
// ***************************************************************
bool publishData()
{
    bool pubSuccess = false;
    if (not(Particle.connected()))
    {
        Log.info("(publishData) NOT CONNECTED! Connecting to Particle cloud.");
        Particle.connect();
    }
    if (waitFor(Particle.connected, 180000))
    {
        Log.info("(publishData) Connected to Particle cloud!");
        syncCloudTime();
        if (myEventName == "")
        {
            myID = System.deviceID();
            getDeviceEventName(myID);
        }              // Sync time with cloud as required
        checkSignal(); // Read cellular signal strength and quality
        Log.info("(publishData) CheckSignal completed Ok!");
        if (newGenTimestamp == 0 || Time.year(newGenTimestamp) > 2030)
        {
            newGenTimestamp = Time.now();
        }
        Log.info("(publishData) Prepare the publish -> makeJSON");
        String msg = makeJSON(noSerie, newGenTimestamp, myEventName, VacuumInHg[0], VacuumInHg[1], VacuumInHg[2], VacuumInHg[3],
                              ExtTemp, lightIntensityLux, fuel.getSoC(), fuel.getVCell(), signalRSSI, signalQuality, BatteryTemp);
        Log.info("(publishData) Publishing now...");
        pubSuccess = Particle.publish(myEventName, msg, PRIVATE, NO_ACK);
        for (int i = 0; i < 150; i++)
        { // Gives 15 seconds to send the data
            Particle.process();
            delay(100);
        }
        if (pubSuccess)
        {
            Log.info("(publishData) Published success!");
            // Log.info("(publishData) Incrementing noSerie now");
            noSerie++;
            SigSystemResetCount = 0;
        }
        else
        {
            FailCount++;
            Log.warn("(publishData) Published fail! Count: %d :(", FailCount);
            delay(500);
        }
    }
    else
    {
        Log.info("(publishData) Attempt to connect timeout after 60 seconds. Not connected!");
        Log.info("(publishData) Publish cancelled!!!");
    }
    return pubSuccess;
}

// ***************************************************************
/// @brief readThermistor: Read and log the temperature
// ***************************************************************
float readThermistor(int NSamples, int interval, String SelectThermistor)
{
    // float thermistorRawValue;
    digitalWrite(thermistorPowerPin, true); // Turn ON thermistor Power
    delay(10UL);                            // Wait for voltage to stabilize
    float sum = 0;
    double temp = 0;
    for (int i = 0; i < NSamples; i++)
    {
        delay(interval);                    // Delay between successives readings
                                            // if (SelectThermistor == "Ext")
                                            // {
        sum += ext_thermistor->readTempC(); // Read temperature and accumulate the readings
    }
    temp = thermistorSlope * (sum / NSamples) + thermistorOffset; // Average the readings and correct the offset
    if (SelectThermistor == "Ext")
    {
        Log.trace("(readThermistor) Temperature Ext. = %.1f°C", temp); // Log final value at info level
    }
    else
    {
        Log.trace("(readThermistor) Temperature Bat. = %.1f°C", temp); // Log final value at info level
    }
    digitalWrite(thermistorPowerPin, false); // Turn OFF thermistor
    return (float)temp;
}

// ***************************************************************
/// @brief readSI7051: Read the SI7051 chip and log the temperature
// ***************************************************************
float readSI7051()
{
    Wire.beginTransmission(0x40);
    Wire.write(0xF3); // calling for Si7051 to make a temperature measurement
    Wire.endTransmission();

    delay(20); // 14 bit temperature conversion needs 10+ms time to complete.

    Wire.requestFrom(0x40, 2);
    delay(25);
    byte msb = Wire.read();
    byte lsb = Wire.read();
    uint16_t val = msb << 8 | lsb;
    float temperature = ((175.72 * val) / 65536) - 46.85;
    if (temperature > 70.0)
    {
        temperature = 7.7777;
    }
    Log.trace("(readSI7051) Si7051 Temperature: %.2f°C", temperature);
    return temperature;
}

// ***************************************************************
/// @brief Read and average vacuum readings
// ***************************************************************
bool readVacuums()
{
    float VacuumRawData[] = {0, 0, 0, 0};      // Vacuum raw data array
    digitalWrite(vacuum5VoltsEnablePin, true); // Turn ON the vacuum trasnducer
    minActualVacuum = 100;                     // Reset min to a high value initially
    delay(25UL);                               // Wait 25 ms for the vacuum sensors to stabilize
    /* Read and log the vacuum values for the four (4) sensors */
    for (int i = 0; i < NVac; i++)
    {
        Particle.process();
        VacuumRawData[i] = AverageReadings(VacuumPins[i], NUMSAMPLES, SAMPLEsINTERVAL); // Average multiple raw readings to reduce noise
        Log.trace("(readVacuums) Vacuum_%d_raw = %.0f", i, VacuumRawData[i]);           // Log raw data at trace level for debugging
        VacuumInHg[i] = VacRaw2inHg(VacuumRawData[i]);
        if (VacuumInHg[i] < -30)
        {
            VacuumInHg[i] = 0;
        };                                                                  // Convert to inHg
        Log.trace("(readVacuums) Vacuum_%d = %.1f inHg", i, VacuumInHg[i]); // Log final value at info level
        minActualVacuum = min(minActualVacuum, VacuumInHg[i]);              // Find the minimum readings of all sensors
    }
    Log.info("(readVacuums) Min Vacuum readings %.1f inHg", minActualVacuum); // Log final value at info level
    digitalWrite(vacuum5VoltsEnablePin, false);                               // Turn OFF the pressure transducers
    // Check if there was a significant change in the vacuum readings
    bool vacChanged = false;
    for (int i = 0; i < NVac; i++)
    {
        if (abs(PrevVacuumInHg[i] - VacuumInHg[i]) > VacMinChange)
        {
            vacChanged = true;
            break;
        }
    }
    for (int i = 0; i < NVac; i++)
    {
        PrevVacuumInHg[i] = VacuumInHg[i]; // Remember previous vacuum readings
    }
    return vacChanged;
}

/// @brief  Convert ADC raw value to Kpa or inHg
// ***************************************************************
/* Convert ADC raw value to Kpa or inHg
 * From MPXV6115V6U datasheet
 * Vout = Vs * (K_fact * Vac + 0.92)
 * where K_fact = 0.007652
 * thus : Vac_kpa = (Vout / Vs * K_fact) - (0,92 / K_fact)
 * and Vout = (Vref*Vraw/4095UL)*(r1+r2)/r2
 * To convert kpa to Hg (inch of mercury) multiply by 0.295301
 */
// ***************************************************************
double VacRaw2inHg(float raw)
{
    double Vout = (Vref * raw / 4095.0f) * (R1 + R2) / R2;   // Vout = Vref*Vraw*(r1_+r2_)/(4096*r2_)
    double Vac_kpa = (Vout / (K_fact * Vs)) - 0.92 / K_fact; // Vac_kpa = (Vout-(Vs-0,92))/(Vs*k)
    double Vac_inHg = Vac_kpa * 0.2953001;
    return Vac_inHg; // multiplie par 0.295301 pour avoir la valeur en Hg
}

// ***************************************************************
/// @brief   Read and log the ambieant light intensity
// ***************************************************************
float readLightIntensitySensor()
{
    float lightRawValue;
    float lightIntensity;
    lightRawValue = AverageReadings(lightSensorInputPin, NUMSAMPLES, 10);          // Average multiple raw readings to reduce noise
    Log.trace("(readLightIntensitySensor) lightRawValue = %.0f", lightRawValue);   // Log raw data at trace level for debugging
    lightIntensity = LightRaw2Lux(lightRawValue);                                  // Convert to Lux
    Log.trace("(readLightIntensitySensor) Light int. = %.0f Lux", lightIntensity); // Log final value at info level
    return lightIntensity;
}

// ***************************************************************
// Check supply input voltage
// ***************************************************************
// float readVin() {
//     float raw = AverageReadings(VinPin, 5, 1);                              // Average 5 readings at 1 ms interval
//     float Sf = 4.012;                                                   // Precise value TDB
//     float Vin = Sf * Vcc * raw / 4095.0f;
//     return Vin;
// }

// ***************************************************************
/* Convert ADC numerical value to light intensity in Lux for the APDS-9007 chip
 * The resistor LOADRESISTOR is used to convert (Iout) the output current to a voltage
 * that is measured by the analog to digital converter. This chip is logarithmic (base 10).
 */
// ***************************************************************
double LightRaw2Lux(float raw)
{
    double Iout = (Vcc * raw / 4095.0f) / LOADRESISTOR; // Calculate the chip output current from raw data
    Log.trace("(LightRaw2Lux) Iout = %.6f A", Iout);    // Log (trace) intermediate results for debugging
    double Lux = pow(10.0f, Iout / 0.00001f);           // Compute the value in LUX
    return Lux;
}

// ***************************************************************
/// @brief   Acquire N readings of an analog input and compute the average
// ***************************************************************
float AverageReadings(int anInputPinNo, int NSamples, int interval)
{
    float sum = 0;
    for (int i = 0; i < NSamples; i++)
    {
        delay(interval);                 // In case a delay is required between successives readings
        sum += analogRead(anInputPinNo); // Read data from selected analog input and accumulate the readings
        Particle.process();
    }
    return sum / NSamples; // Divide the sum by the number of readings
}

// ***************************************************************
/// @brief   Formattage standard pour les données sous forme JSON
// ***************************************************************
String makeJSON(int numSerie, int timeStamp, String eName, float va, float vb, float vc, float vd, float temp, float li, float soc, float volt, float RSSI, float signalQual, float BatTemp)
{
    char publishString[255];
    sprintf(publishString, "{\"noSerie\": %d,\"generation\": %d,\"eName\": \"%s\",\"va\":%.1f,\"vb\":%.1f,\"vc\":%.1f,\"vd\":%.1f,\"temp\":%.1f,\"li\":%.0f,\"soc\":%.2f,\"volt\":%.3f,\"rssi\":%.0f,\"qual\":%.0f,\"batTemp\":%.1f}",
            numSerie, newGenTimestamp, eName.c_str(), VacuumInHg[0], VacuumInHg[1], VacuumInHg[2], VacuumInHg[3], ExtTemp, lightIntensityLux, soc, volt, RSSI, signalQual, BatTemp);
    Log.info("(makeJSON) ");
    Log.print(strcat(publishString, "\r\n"));
    return publishString;
}

// ***************************************************************
/// @brief  Read cellular signal strength and quality
// ***************************************************************
bool checkSignal()
{
    CellularSignal sig = Cellular.RSSI();
    // signalRSSI = sig.rssi;
    signalRSSI = sig.getStrengthValue();

    // signalQuality = sig.qual;
    signalQuality = sig.getQualityValue();
    // String s = "RSSI.QUALITY: \t" + String(signalRSSI) + "\t" + String(signalQuality) + "\t";
    if (signalRSSI == 0 || signalQuality == 0)
    {
        Log.info("(checkSignal) NETWORK CONNECTION LOST!!");
        delay(2000UL);
        if (SigSystemResetCount <= 3)
        {
            SigSystemResetCount++;
            System.reset();
        }
        return false;
    }
    else if (signalRSSI == 1)
    {
        Log.info("(checkSignal) Cellular module or time-out error");
        return false;
    }
    else if (signalRSSI == 2)
    {
        Log.info("(checkSignal) RSSI value is not known, not detectable or currently not available");
        return false;
    }
    else
    {
        Log.info("(checkSignal) Read the cellular signal!, %.0f, %.0f", signalRSSI, signalQuality);
        return true;
    }
}

// ***************************************************************
/// @brief   Synchronize clock with cloud one a day
// ***************************************************************
void syncCloudTime()
{
    if (Time.day() != lastDay || Time.year() < currentYear)
    { // a new day calls for a sync
        Log.info("(syncCloudTime) Sync time");
        if (Particle.connected())
        {
            Particle.syncTime();
            start = millis();
            while (millis() - start < 1000UL)
            {
                delay(20);
                Particle.process(); // Wait a second to received the time.
            }
            lastDay = Time.day();
            currentYear = Time.year();
            Log.info("(syncCloudTime) Sync time completed");
        }
        else
        {
            Log.warn("(syncCloudTime) Failed to connect! Try again in 2 miutes");
            SigSystemResetCount++;
            Log.warn("(syncCloudTime) SETUP Restart. Count: %d, battery: %.1f", SigSystemResetCount, fuel.getSoC());
            setSleepState(SLEEP_5_MINUTES);
        }
    }
    // RGB.mirrorDisable();
}

// ***************************************************************
/// @brief   Adjust charger current base on temperature
// ***************************************************************
void configCharger(bool mode)
{
    PMIC pmic; // Initalize the PMIC class so you can call the Power Management functions below.
    BatteryTemp = readSI7051();
    // In case of malfunction of SI7051 BatteryTemp = 7.7777°C
    if (BatteryTemp >= 7.76 && BatteryTemp <= 7.78)
    {
        BatteryTemp = readThermistor(1, 1, "Bat");
    }
    // Mode true: normal operation
    pmic.setInputVoltageLimit(4840); // Set the lowest input voltage to 4.84 volts best setting for 6V solar panels
    if (mode == true)
    {
        if (BatteryTemp <= 1.0 or BatteryTemp >= 40.0)
        {
            if (chargerStatus != off)
            {
                // Disable charger to protect the battery
                pmic.setChargeVoltage(ChargeVoltageMaxLevel_cold); // Set charge voltage to 100%
                pmic.setChargeCurrent(0, 0, 0, 0, 0, 0);           // Set charging current to 512mA (512 + 0 offset)
                pmic.disableCharging();
                chargerStatus = off;
                Log.info("(configCharger) Battery temperature: %0.1f - Disable charger", BatteryTemp);
            }
        }
        else if (BatteryTemp <= 5.0)
        {
            if (chargerStatus != lowCurrent)
            {
                // Reduce the charge current
                pmic.setChargeVoltage(ChargeVoltageMaxLevel_cold); // Set charge voltage to 100%
                pmic.setChargeCurrent(0, 0, 0, 0, 0, 1);           // Set charging current to 576mA (512 + 64 offset)
                pmic.enableCharging();
                chargerStatus = lowCurrent;
                Log.info("(configCharger) Battery temperature: %0.1f - Set charger to low current. ChargeVoltage: %d", BatteryTemp, ChargeVoltageMaxLevel_warm);
            }
        }
        else if (BatteryTemp >= 20.0)
        {
            // Charge at full current
            pmic.setChargeVoltage(ChargeVoltageMaxLevel_warm); // Set charge voltage to Particle recommended voltage
            pmic.setChargeCurrent(0, 1, 0, 0, 0, 0);           // Set charging current to 1024mA (1024 offset)
            pmic.enableCharging();
            chargerStatus = highCurrent;
            Log.info("(configCharger) Battery temperature: %0.1f - Set charger to high current. ChargeVoltage: %d", BatteryTemp, ChargeVoltageMaxLevel_warm);
        }
        else
        {
            if (chargerStatus != highCurrent)
            {
                // Charge at full current
                pmic.setChargeVoltage(ChargeVoltageMaxLevel_warm); // Set charge voltage to 100%
                pmic.setChargeCurrent(0, 1, 0, 0, 0, 0);           // Set charging current to 1024mA (1024 + 512 offset)
                chargerStatus = highCurrent;
                pmic.enableCharging();
                Log.info("(configCharger) Battery temperature: %0.1f - Set charger to high current. ChargeVoltage: %d", BatteryTemp, ChargeVoltageMaxLevel_warm);
            }
        }
    }
    else
    {
        // Mode false: Disable charger to protect the battery for night sleep
        if (chargerStatus != off)
        {
            // Disable charger to protect the battery
            pmic.setChargeVoltage(ChargeVoltageMaxLevel_cold); // Set charge voltage to 100%
            pmic.setChargeCurrent(0, 0, 0, 0, 0, 0);           // Set charging current to 1024mA (512 + 512 offset)
            pmic.disableCharging();
            chargerStatus = off;
            Log.info("(configCharger) Battery temperature: %0.1f - Disable charger", BatteryTemp);
        }
    }
}

// ***************************************************************
/// @brief   Remotely set thermistor offset
// ***************************************************************
int setThermistorOffset(String offsetValue)
{
    thermistorOffset = offsetValue.toFloat();
    EEPROM.put(ThermistorOffsetAddress, thermistorOffset);
    uint32_t validThermCalib = 0x0001;
    EEPROM.put(validCalibAddress, validThermCalib);
    Log.info("(setThermistorOffset) Set thermistor offset to: %s, %0.3f", offsetValue.c_str(), thermistorOffset);
    ExtTemp = readThermistor(1, 1, "Ext");
    publishData();
    return 0;
}

// ***************************************************************
/// @brief   Remotely set thermistor slope
// ***************************************************************
int setThermistorSlope(String slopeValue)
{
    thermistorSlope = slopeValue.toFloat();
    EEPROM.put(ThermistorSlopeAddress, thermistorSlope);
    uint32_t validThermCalib = 0x0001;
    EEPROM.put(validCalibAddress, validThermCalib);
    Log.info("(setThermistorSlope) Set thermistor slope to: %s, %0.3f", slopeValue.c_str(), thermistorSlope);
    ExtTemp = readThermistor(1, 1, "Ext");
    publishData();
    return 0;
}

// ***************************************************************
/// @brief   Remotely set/reset auto calibration flag
// ***************************************************************
int setAutoCalibFlag(String state)
{
    if (state == "true")
    {
        enableAutoCalib = true;
        Log.info("enableAutoCalib is set to: %s", state.c_str());
        return 1;
    }
    else if (state == "false")
    {
        enableAutoCalib = false;
        Log.info("enableAutoCalib is set to: %s", state.c_str());
        return 0;
    }
    else
    {
        return -1;
    }
}

// ***************************************************************
/// @brief   Remotely set/reset night sleep flag
// ***************************************************************
int setNightSleepFlag(String state)
{
    if (state == "true")
    {
        NightSleepFlag = true;
        Log.info("NightSleepFlag is set to: %s", state.c_str());
        return 1;
    }
    else if (state == "false")
    {
        NightSleepFlag = false;
        Log.info("NightSleepFlag is set to: %s", state.c_str());
        return 0;
    }
    else
    {
        return -1;
    }
}

// ***************************************************************
/// @brief   Remotely set/reset firmware update flag
// ***************************************************************
int setSoftUpdateFlag(String state)
{
    if (state == "true")
    {
        SoftUpdateDisponible = true;
        updateCounter = UpdateCounterInit; // Stay aware of software update available for some cycle
        Log.info("SoftUpdateDisponible is set to: %s", state.c_str());
        return 1;
    }
    else if (state == "false")
    {
        SoftUpdateDisponible = false;
        updateCounter = 0; // Do not stay aware of software update
        Log.info("SoftUpdateDisponible is set to: %s", state.c_str());
        return 0;
    }
    else
    {
        return -1;
    }
}
// Dev name      No de lignes
//  VA1-4     4  Lignes A1 à A4 RSSI = -77, qual 37
//  VA5B1-2   3  Lignes A5, B1 et B2
//  VC1-3     3  Lignes C1 à C3
//  VC4-6     3  Lignes C4 à C6
//  VC7-8     2  Lignes C7 et C8
//  VD1A-2B   4  Lignes D1A, D1B, D2A et D2B
//  VE1-3     3  Lignes E1 à E3
//  VE4-6     3  Lignes E4 à E6
//  VE7-9     3  Lignes E7 à E9
//  VE10-12   3  Lignes E10 à E12
//  VF1-3     3  Lignes F1 à F3
//  VF4-6     3  Lignes F4 à F6
//  VF7-9     3  Lignes F7 à F9 RSSI = -81, qual 37
//  VF10-12   3  Lignes F10 à F12
//  VF13-16   4  Lignes F13 à F16
//  VG1-2-H14 3  Lignes G1, G2 et H14
//  VG3-5     3  Lignes G3 à G5
//  VG6-8     3  Lignes G6 à G8
//  VG9-12    4  Lignes G9 à G12
//  VH2-4     3  Lignes H2 à H4
//  VH5-7     3  Lignes H5 à H7
//  VH8-10    3  Lignes H8 à H10
//  VH11-13   3  Lignes H11 à H13 RSSI = -91, qual 19

// ***************************************************************
/// @brief   Get the device name from the device ID
// ***************************************************************
void getDeviceEventName(String devId)
{
    // std::unordered_map<std::string, String> deviceMap;
    // deviceMap.insert({"36004f000251353337353037", "EB-Elec-Dev1"});
    // deviceMap.insert({"240051000c51343334363138", "EB-VF7-9"});
    // myNameIs = deviceMap[devId];
    // if (devId == "36004f000251353337353037"){
    //     myNameIs = "EB-Elec-Dev1";
    //     myEventName = "test1_Vacuum/Lignes";
    // } else {
    //     myEventName = "Vacuum/Lignes";
    // }
    Log.info("Device Name is: " + myNameIs);
    Log.info("Event Name is: " + myEventName);
}
