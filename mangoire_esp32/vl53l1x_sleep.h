#ifndef VL53L1X_SLEEP_H
#define VL53L1X_SLEEP_H

#include <Wire.h>
#include "VL53L1X_ULD.h"
#include "esp_sleep.h"
#include <Arduino.h>

#define I2C_SDA 14
#define I2C_SCL 15
#define VL53L1X_I2C_ADDR 0x52
#define VL53L1X_INT_PIN GPIO_NUM_13

static uint8_t booted = 0;

inline void setupVL53L1XAndSleep()
{
      Wire.begin(I2C_SDA, I2C_SCL);
      Serial.println("VL53L1X Test avec ESP32");
      while (booted == 0)
      {
            VL53L1X_BootState(VL53L1X_I2C_ADDR, &booted);
            delay(2);
      }
      Serial.println("Capteur booté");
      VL53L1X_SensorInit(VL53L1X_I2C_ADDR);
      VL53L1X_SetDistanceMode(VL53L1X_I2C_ADDR, 1);
      VL53L1X_SetTimingBudgetInMs(VL53L1X_I2C_ADDR, 50);
      VL53L1X_SetInterMeasurementInMs(VL53L1X_I2C_ADDR, 100);
      VL53L1X_SetInterruptPolarity(VL53L1X_I2C_ADDR, 0);
      /*
            VL53L1X_SetDistanceThreshold arguments :
                  1. Adresse I2C du capteur (ici 0x52)
                  2. Seuil bas (en mm)
                  3. Seuil haut (en mm)
                  4. Mode de fenêtre (window mode) :
                         0 = OUTSIDE (interruption si la mesure est < seuil bas OU > seuil haut)
                         1 = INSIDE  (interruption si la mesure est entre les deux seuils)
                         2 = BELOW   (interruption si la mesure est < seuil haut)
                         3 = ABOVE   (interruption si la mesure est > seuil haut)
                  5. (optionnel) dépend du firmware/librairie (souvent 0)

            Exemple pour réveil si objet détecté à moins de 50 cm :
            VL53L1X_SetDistanceThreshold(VL53L1X_I2C_ADDR, 0, 500, 2, 0);
      */
      VL53L1X_SetDistanceThreshold(VL53L1X_I2C_ADDR, 0, 500, 2, 0);
      VL53L1X_StartRanging(VL53L1X_I2C_ADDR);

      // Mesure immédiate avant deep sleep
      uint16_t distance = 0;
      uint8_t dataReady = 0;
      // Attendre que la mesure soit prête
      while (dataReady == 0)
      {
            VL53L1X_CheckForDataReady(VL53L1X_I2C_ADDR, &dataReady);
            delay(2);
      }
      VL53L1X_GetDistance(VL53L1X_I2C_ADDR, &distance);
      VL53L1X_ClearInterrupt(VL53L1X_I2C_ADDR);
      Serial.print("Distance initiale: ");
      Serial.print(distance);
      Serial.println(" mm");
      // Tant qu'un objet est présent (< 500mm), on ne dort pas
      while (distance < 500)
      {
            delay(100);
            dataReady = 0;
            while (dataReady == 0)
            {
                  VL53L1X_CheckForDataReady(VL53L1X_I2C_ADDR, &dataReady);
                  delay(2);
            }
            VL53L1X_GetDistance(VL53L1X_I2C_ADDR, &distance);
            VL53L1X_ClearInterrupt(VL53L1X_I2C_ADDR);
            Serial.print("Distance: ");
            Serial.print(distance);
            Serial.println(" mm (attente objet parti)");
      }

      // Quand plus d'objet, on peut dormir
      esp_sleep_enable_ext0_wakeup((gpio_num_t)VL53L1X_INT_PIN, 0); // wake on LOW
      Serial.println("Mise en deep sleep, attente d'un objet...");
      delay(100);
      pinMode(13, INPUT);
      while (digitalRead(13) == LOW)
      {
            Serial.println("Attente que l'interruption repasse à HIGH...");
            delay(10);
      }
      esp_deep_sleep_start();
}

#endif // VL53L1X_SLEEP_H
