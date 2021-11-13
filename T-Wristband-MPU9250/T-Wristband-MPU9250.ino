
#include <pcf8563.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include "sensor.h"
#include "esp_adc_cal.h"
#include "assets/charge.h"
#include "assets/battery1.h"
#include "assets/battery2.h"
#include "assets/battery3.h"
#include "assets/battery4.h"

//  git clone -b development https://github.com/tzapu/WiFiManager.git
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager

#define ARDUINO_OTA_UPDATE      //! Enable this line OTA update


#ifdef ARDUINO_OTA_UPDATE
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#endif


#define TP_PIN_PIN          33
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define IMU_INT_PIN         38
#define RTC_INT_PIN         34
#define BATT_ADC_PIN        35
#define VBUS_PIN            36
#define TP_PWR_PIN          25
#define LED_PIN             4 //Led and Vibration
#define CHARGE_PIN          32

extern MPU9250 IMU;

TFT_eSPI tft = TFT_eSPI();  // Invoke library, pins defined in User_Setup.h
PCF8563_Class rtc;
WiFiManager wifiManager;

char buff[256];
bool rtcIrq = false;
bool initial = 1;
bool initial_action = 1;
bool update_func = 0;
bool otaStart = false;

uint8_t func_select = 0;
uint8_t omm = 99;
uint8_t xcolon = 0;
uint32_t targetTime = 0;       // for next 1 second timeout
uint32_t colour = 0;
int vref = 1100;

bool pressed = false;
uint32_t pressedTime = 0;
uint32_t lastTimePress = 0;
int inactiveTime = 10000;
bool dont_sleep = 0;
int actionTime = 1000;
bool charge_indication = false;
bool battery_indication = true;

int battery_state = 0;

uint8_t hh, mm, ss ;

void configModeCallback (WiFiManager *myWiFiManager)
{
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    //if you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Connect hotspot name ",  20, tft.height() / 2 - 20);
    tft.drawString("configure wrist",  35, tft.height() / 2  + 20);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("\"T-Wristband\"",  40, tft.height() / 2 );

}

void drawProgressBar(uint16_t x0, uint16_t y0, uint16_t w, uint16_t h, uint8_t percentage, uint16_t frameColor, uint16_t barColor)
{
    if (percentage == 0) {
        tft.fillRoundRect(x0, y0, w, h, 3, TFT_BLACK);
    }
    uint8_t margin = 2;
    uint16_t barHeight = h - 2 * margin;
    uint16_t barWidth = w - 2 * margin;
    tft.drawRoundRect(x0, y0, w, h, 3, frameColor);
    tft.fillRect(x0 + margin, y0 + margin, barWidth * percentage / 100.0, barHeight, barColor);
}

void setupWiFi()
{
#ifdef ARDUINO_OTA_UPDATE
    WiFiManager wifiManager;
    //set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);
    wifiManager.setBreakAfterConfig(true);          // Without this saveConfigCallback does not get fired
    wifiManager.autoConnect("T-Wristband");
#endif
}

void setupOTA()
{
#ifdef ARDUINO_OTA_UPDATE
    // Port defaults to 3232
    // ArduinoOTA.setPort(3232);

    // Hostname defaults to esp3232-[MAC]
    ArduinoOTA.setHostname("T-Wristband");

    // No authentication by default
    // ArduinoOTA.setPassword("admin");

    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH)
            type = "sketch";
        else // U_SPIFFS
            type = "filesystem";

        // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
        Serial.println("Start updating " + type);
        otaStart = true;
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Updating...", tft.width() / 2 - 20, 55 );
    })
    .onEnd([]() {
        Serial.println("\nEnd");
        delay(500);
    })
    .onProgress([](unsigned int progress, unsigned int total) {
        // Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
        int percentage = (progress / (total / 100));
        tft.setTextDatum(TC_DATUM);
        tft.setTextPadding(tft.textWidth(" 888% "));
        tft.drawString(String(percentage) + "%", 145, 35);
        drawProgressBar(10, 30, 120, 15, percentage, TFT_WHITE, TFT_BLUE);
    })
    .onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
        else if (error == OTA_END_ERROR) Serial.println("End Failed");

        tft.fillScreen(TFT_BLACK);
        tft.drawString("Update Failed", tft.width() / 2 - 20, 55 );
        delay(3000);
        otaStart = false;
        initial = 1;
        targetTime = millis() + 1000;
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(TL_DATUM);
        omm = 99;
    });

    ArduinoOTA.begin();
#endif
}


void setupADC()
{
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC1_CHANNEL_6, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
    //Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.printf("eFuse Vref:%u mV", adc_chars.vref);
        vref = adc_chars.vref;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
    } else {
        Serial.println("Default Vref: 1100mV");
    }
}

void setupRTC()
{
    rtc.begin(Wire);
    //Check if the RTC clock matches, if not, use compile time
    rtc.check();

    RTC_Date datetime = rtc.getDateTime();
    hh = datetime.hour;
    mm = datetime.minute;
    ss = datetime.second;
}

void setup(void)
{
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1);
    tft.setSwapBytes(true);

    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    setupRTC();

    setupMPU9250();

    setupADC();

    tft.fillScreen(TFT_BLACK);

    tft.setTextColor(TFT_YELLOW, TFT_BLACK); // Note: the new fonts do not draw the background colour

    targetTime = millis() + 1000;

    pinMode(TP_PIN_PIN, INPUT);
    //! Must be set to pull-up output mode in order to wake up in deep sleep mode
    pinMode(TP_PWR_PIN, PULLUP);
    digitalWrite(TP_PWR_PIN, HIGH);

    pinMode(LED_PIN, OUTPUT);

    pinMode(CHARGE_PIN, INPUT_PULLUP);
    attachInterrupt(CHARGE_PIN, [] {
        charge_indication = true;
        battery_indication = !charge_indication;
    }, CHANGE);

    if (digitalRead(CHARGE_PIN) == LOW) {
        charge_indication = true;
        battery_indication = !charge_indication;
    }
}

float getVoltage()
{
    uint16_t v = analogRead(BATT_ADC_PIN);
    float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
    return battery_voltage;
}

float calculateBatteryPerc(float maxi, float mini, float pmaxi, float pmini, float volt)
{
  // Calculates an approximate value of the precentage
  float ratio = (volt - mini) * 100 / (maxi - mini);
  return ((pmaxi - pmini) * ratio) / 100 + pmini;
}

float getBatteryPerc()
{
  // Gets the percentage of battery capacity based on its voltage (considering the voltage analysis of a lithium battery and its relationship with its capacity).
  // https://www.programmerclick.com/article/83262164670/
  // The voltage roughly goes from 4.34 to 2.5
  float volt = getVoltage();
  float p;
  
  if (volt >= 4.2)
    p = 100;
  else if (volt >= 4.08)
    p = calculateBatteryPerc(4.2, 4.08, 100, 90, volt);
  else if (volt >= 4)
    p = calculateBatteryPerc(4.08, 4, 90, 80, volt);
  else if (volt >= 3.93)
    p = calculateBatteryPerc(4, 3.93, 80, 70, volt);
  else if (volt >= 3.87)
    p = calculateBatteryPerc(3.93, 3.87, 70, 60, volt);
  else if (volt >= 3.82)
    p = calculateBatteryPerc(3.87, 3.82, 60, 50, volt);
  else if (volt >= 3.79)
    p = calculateBatteryPerc(3.82, 3.79, 50, 40, volt);
  else if (volt >= 3.77)
    p = calculateBatteryPerc(3.79, 3.77, 40, 30, volt);
  else if (volt >= 3.73)
    p = calculateBatteryPerc(3.77, 3.73, 30, 20, volt);
  else if (volt >= 3.7)
    p = calculateBatteryPerc(3.73, 3.7, 20, 15, volt);
  else if (volt >= 3.68)
    p = calculateBatteryPerc(3.7, 3.68, 15, 10, volt);
  else if (volt >= 3.5)
    p = calculateBatteryPerc(3.68, 3.5, 10, 5, volt);
  else if (volt >= 2.5)
    p = calculateBatteryPerc(3.5, 2.5, 5, 0, volt);
  else
    p = 0;

  return p;
}

void RTC_Show()
{
    if (targetTime < millis()) {
        RTC_Date datetime = rtc.getDateTime();
        hh = datetime.hour;
        mm = datetime.minute;
        ss = datetime.second;
        // Serial.printf("hh:%d mm:%d ss:%d\n", hh, mm, ss);
        targetTime = millis() + 1000;
        if (ss == 0 || initial) {
            initial = 0;
            if (digitalRead(CHARGE_PIN) == LOW) {
                charge_indication = true;
            } else {
                battery_indication = true;
            }
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.setCursor (8, 60);
            tft.print(__DATE__); // This uses the standard ADAFruit small font
        }

        tft.setTextColor(TFT_BLUE, TFT_BLACK);
        
        float per = getBatteryPerc();
        if (digitalRead(CHARGE_PIN) != LOW) {
          if (per >= 0 && per <= 10 && battery_state != 4) {
            battery_indication = true;
          }
          else if (per > 10 && per <= 30 && battery_state != 3) {
            battery_indication = true;
          }
          else if (per > 30 && per <= 50 && battery_state != 2) {
            battery_indication = true;
          }
          else if (per > 50 && per <= 100 && battery_state != 1) {
            battery_indication = true;
          }
        }
        tft.drawCentreString(String(per) + "%", 120, 60, 1); // Next size up font 2


        // Update digital time
        uint8_t xpos = 6;
        uint8_t ypos = 0;
        if (omm != mm) { // Only redraw every minute to minimise flicker
            // Uncomment ONE of the next 2 lines, using the ghost image demonstrates text overlay as time is drawn over it
            tft.setTextColor(0x39C4, TFT_BLACK);  // Leave a 7 segment ghost image, comment out next line!
            //tft.setTextColor(TFT_BLACK, TFT_BLACK); // Set font colour to black to wipe image
            // Font 7 is to show a pseudo 7 segment display.
            // Font 7 only contains characters [space] 0 1 2 3 4 5 6 7 8 9 0 : .
            tft.drawString("88:88", xpos, ypos, 7); // Overwrite the text to clear it
            tft.setTextColor(0xFBE0, TFT_BLACK); // Orange
            omm = mm;

            if (hh < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
            xpos += tft.drawNumber(hh, xpos, ypos, 7);
            xcolon = xpos;
            xpos += tft.drawChar(':', xpos, ypos, 7);
            if (mm < 10) xpos += tft.drawChar('0', xpos, ypos, 7);
            tft.drawNumber(mm, xpos, ypos, 7);
        }

        if (ss % 2) { // Flash the colon
            tft.setTextColor(0x39C4, TFT_BLACK);
            xpos += tft.drawChar(':', xcolon, ypos, 7);
            tft.setTextColor(0xFBE0, TFT_BLACK);
        } else {
            tft.drawChar(':', xcolon, ypos, 7);
        }
    }
}

void print_battery_level() {
  tft.fillRect(140, 55, 16, 16, TFT_BLACK);
  battery_indication = false;
  float percentage = getBatteryPerc();
  
  if (percentage <= 10) {
    battery_state = 4;
    tft.pushImage(144, 55, 8, 16, battery4); //posx, posy, width, height, var
  }
  else if (percentage <= 30) {
    battery_state = 3;
    tft.pushImage(144, 55, 8, 16, battery3);
  }
  else if (percentage <= 50) {
    battery_state = 2;
    tft.pushImage(144, 55, 8, 16, battery2);
  }
  else {
    battery_state = 1;
    tft.pushImage(144, 55, 8, 16, battery1);
  }
}

void OTA_begin()
{
  if (initial_action) {
    dont_sleep = 1;
    initial_action = 0;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("OTA start",  50, tft.height() / 2 );

    setupWiFi();
    setupOTA();
  }
}

void OTA_page()
{
  if (initial) {
    initial = 0;
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Press for 1s",  42, tft.height() / 2 - 10);
    tft.drawString("to access OTA",  40, tft.height() / 2  + 10);
  }
}

void resetWiFi() {
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Reset WiFi Setting",  20, tft.height() / 2 );
  delay(3000);
  wifiManager.resetSettings();
  wifiManager.erase(true);
  esp_restart();
}

void go_to_sleep()
{
    tft.fillScreen(TFT_BLACK);
    IMU.setSleepEnabled(true);
    //Serial.println("Go to Sleep");
    delay(1000);
    tft.writecommand(ST7735_SLPIN);
    tft.writecommand(ST7735_DISPOFF);
    esp_sleep_enable_ext1_wakeup(GPIO_SEL_33, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_deep_sleep_start();
}

void page_action()
{
  switch (func_select) {
    case 0:
        go_to_sleep();
        break;
    case 1:
        OTA_begin();
        break;
    default:
        break;
    }
}

void loop()
{
#ifdef ARDUINO_OTA_UPDATE
    ArduinoOTA.handle();
#endif

    //! If OTA starts, skip the following operation
    if (otaStart)
        return;

    if (!dont_sleep && (millis() - lastTimePress > inactiveTime))
      go_to_sleep();

    if (charge_indication) {
        charge_indication = false;
        if (digitalRead(CHARGE_PIN) == LOW) {
            tft.pushImage(140, 55, 16, 16, charge);
        } else {
            print_battery_level();
        }
    }
    else if (battery_indication) {
      print_battery_level();
    }


    if (digitalRead(TP_PIN_PIN) == HIGH) {
        if (!pressed) {
            initial_action = 1;
            update_func = 1;
            pressed = true;
            pressedTime = millis();
        } else {
            if (millis() - pressedTime > actionTime) {
              update_func = 0;
              page_action();
            }
        }
    } else if (update_func) {
      initial = 1;
      targetTime = millis() + 1000;
      update_func = 0;
      tft.fillScreen(TFT_BLACK);
      omm = 99;
      func_select = func_select + 1 > 1 ? 0 : func_select + 1;
      //digitalWrite(LED_PIN, HIGH);
      //delay(100);
      //digitalWrite(LED_PIN, LOW);
      dont_sleep = 0;
      lastTimePress = millis();
    } else {
        pressed = false;
    }

    switch (func_select) {
    case 0:
        RTC_Show();
        break;
    case 1:
        OTA_page();
        break;
    default:
        break;
    }
}
