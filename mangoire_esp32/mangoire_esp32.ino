#define ENABLE_VL53L1X 0 // Mettre à 0 pour désactiver le capteur VL53L1X
#if ENABLE_VL53L1X
#include "vl53l1x_sleep.h"
#endif
#include "esp_camera.h"
#include <WiFi.h>
#include <Wire.h>

// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"

#include <Wire.h>
#include "VL53L1X_ULD.h" // Fichiers ST : VL53L1X_api.c/.h et VL53L1X_platform.c/.h doivent être ajoutés au projet

// ===========================
// VL53L1X
// ===========================
#define I2C_SDA 14
#define I2C_SCL 15
#define VL53L1X_I2C_ADDR 0x52       // Adresse par défaut du VL53L1X
#define VL53L1X_INT_PIN GPIO_NUM_13 // GPIO1 du VL53L1X → GPIO13 ESP32

// ===========================
// WiFi credentials (chargés dynamiquement)
// ===========================
#include "config_utils.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

char ssid[64] = "";
char password[64] = "";

void startCameraServer();
void setupLedFlash();

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // ===========================
  // Setup de la caméra et du serveur
  // ===========================

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  // config.pixel_format = PIXFORMAT_RGB565; // for face detection/recognition
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    if (psramFound())
    {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    }
    else
    {
      // Limit the frame size when PSRAM is not available
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  }
  else
  {
    // Best option for face detection/recognition
    config.frame_size = FRAMESIZE_240X240;
#if CONFIG_IDF_TARGET_ESP32S3
    config.fb_count = 2;
#endif
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  Serial.println(psramFound() ? "PSRAM OK" : "PSRAM ABSENTE");

  // Initialisation LittleFS
  if (!LittleFS.begin(true))
  {
    Serial.println("Erreur LittleFS: impossible de monter le système de fichiers");
    return;
  }
  else
  {
    Serial.println("LittleFS monté avec succès");
  }

  // Lecture de la config WiFi depuis /config.json
  StaticJsonDocument<512> doc;
  if (loadConfig(doc))
  {
    const char *s = doc["ssid"] | "";
    const char *p = doc["password"] | "";
    strncpy(ssid, s, sizeof(ssid) - 1);
    strncpy(password, p, sizeof(password) - 1);
    ssid[sizeof(ssid) - 1] = 0;
    password[sizeof(password) - 1] = 0;
    Serial.printf("Config WiFi chargée: ssid='%s'\n", ssid);
  }
  else
  {
    Serial.println("Aucune config WiFi trouvée, ssid/password vides");
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  else
  {
    Serial.println("Camera init OK");
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);       // flip it back
    s->set_brightness(s, 1);  // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  if (config.pixel_format == PIXFORMAT_JPEG)
  {
    s->set_framesize(s, FRAMESIZE_VGA);
  }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  Serial.printf("Tentative connexion WiFi: ssid='%s'\n", ssid);
  WiFi.begin(ssid, password);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 40) // 20s max
  {
    delay(500);
    Serial.print(".");
    wifi_attempts++;
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("");
    Serial.println("WiFi connected");
    startCameraServer();
    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect");
  }
  else
  {
    Serial.println("");
    Serial.println("WiFi non connecté (timeout), passage en mode Access Point");
    WiFi.disconnect(true);
    delay(1000);
    WiFi.mode(WIFI_AP);
    const char *ap_ssid = "BirdCam_Config";
    const char *ap_password = ""; // Pas de mot de passe pour config facile
    bool ap_ok = WiFi.softAP(ap_ssid, ap_password);
    if (ap_ok)
    {
      Serial.print("Point d'accès actif: SSID=");
      Serial.println(ap_ssid);
      Serial.print("IP: ");
      Serial.println(WiFi.softAPIP());
      startCameraServer();
      Serial.print("Camera Ready! Use 'http://");
      Serial.print(WiFi.softAPIP());
      Serial.println("' to connect");
    }
    else
    {
      Serial.println("Echec démarrage AP");
    }
  }

  // ===========================
// VL53L1X (optionnel)
// ===========================
#if ENABLE_VL53L1X
  setupVL53L1XAndSleep();
#endif
  // Deep sleep déjà géré dans setupVL53L1XAndSleep()
}

void loop()
{
  // Do nothing. Everything is done in another task by the web server
}
