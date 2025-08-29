#include <Arduino.h>
#include <esp_heap_caps.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"

#include "board_config.h"
#include "config_utils.h"
#include <ArduinoJson.h>
#include "esp_http_server.h"
#include "esp_camera.h"
#include <ArduinoJson.h>
#include <string.h>
void camera_dma_diagnostics(const char *contexte)
{
  Serial.printf("[DIAG][%s] Heap: %u, PSRAM: %u, PSRAM size: %u\n", contexte, ESP.getFreeHeap(), ESP.getFreePsram(), ESP.getPsramSize());
  sensor_t *s = esp_camera_sensor_get();
  if (s)
  {
    Serial.printf("[DIAG][%s] Capteur PID: 0x%02X, framesize: %d, pixformat: %d\n", contexte, s->id.PID, s->status.framesize, s->pixformat);
  }
  Serial.printf("[DIAG][%s] PSRAM found: %s\n", contexte, psramFound() ? "OUI" : "NON");
  Serial.printf("[DIAG][%s] xclk_freq_hz: %d\n", contexte, XCLK_GPIO_NUM);
}

// Diagnostic après capture
void camera_fb_diagnostics(const camera_fb_t *fb, const char *contexte)
{
  if (fb)
  {
    Serial.printf("[DIAG][%s] Frame OK: len=%u, w=%u, h=%u, format=%u\n", contexte, fb->len, fb->width, fb->height, fb->format);
  }
  else
  {
    Serial.printf("[DIAG][%s] Frame NULL (capture échouée)\n", contexte);
  }
  Serial.printf("[DIAG][%s] Heap: %u, PSRAM: %u\n", contexte, ESP.getFreeHeap(), ESP.getFreePsram());
}
// Page HTML de réglages caméra
static const char settings_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
  <meta charset='UTF-8'>
  <title>Réglages Caméra</title>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <style>
    body{font-family:sans-serif;max-width:900px;margin:2em auto;}
    .flex-row{display:flex;flex-direction:row;align-items:flex-start;gap:2em;}
    #settingsForm{min-width:320px;max-width:400px;flex:1;}
    #liveStream{flex:1;min-width:320px;max-width:480px;}
    input,select,button{font-size:1em;margin:0.5em 0;width:100%;}
  </style>
</head>
<body>
  <div class="flex-row">
    <div>
      <h2>Réglages Caméra</h2>
      <form id='settingsForm'>
        <label>Qualité JPEG (0-63):<input type='number' name='quality' min='0' max='63' value='10'required></label><br>
        <label>Contraste (-2 à 2):<input type='number' name='contrast' min='-2' max='2' value='0' required></label><br>
        <label>Luminosité (-2 à 2):<input type='number' name='brightness' min='-2' max='2' value='0' required></label><br>
        <label>Saturation (-2 à 2):<input type='number' name='saturation' min='-2' max='2' value='0' required></label><br>
        <label>Balance des blancs auto (AWB):
          <select name='awb'>
            <option value='1'>Activé</option>
            <option value='0'>Désactivé</option>
          </select>
        </label><br>
        <label>Contrôle auto du gain (AGC):
          <select name='agc'>
            <option value='1'>Activé</option>
            <option value='0'>Désactivé</option>
          </select>
        </label><br>
        <label>Contrôle auto de l’exposition (AEC):
          <select name='aec'>
            <option value='1'>Activé</option>
            <option value='0'>Désactivé</option>
          </select>
        </label><br>
        <button type='submit'>Sauvegarder</button>
      </form>
      <div id='msg'></div>
    </div>
    <div id="liveStream">
      <img id="streamImg" src="/stream" style="width:100%;max-width:400px;">
    </div>
  </div>
  <script>
    async function loadSettings() {
      try {
        const res = await fetch('/api/settings');
        if(res.ok){
          const s = await res.json();
          for(const k in s){
            // On ne modifie la valeur du champ que si elle existe dans le formulaire
            if(document.forms[0][k] !== undefined && s[k] !== undefined && s[k] !== null) {
              document.forms[0][k].value = s[k];
            }
          }
        }
        // Si le fichier n'existe pas ou la clé n'est pas présente, la valeur par défaut HTML reste
      } catch(e) {
        // Erreur de fetch : on garde les valeurs par défaut du HTML
      }
    }
    document.getElementById('settingsForm').onsubmit = async e => {
      e.preventDefault();
      const data = Object.fromEntries(new FormData(e.target).entries());
      for(const k in data) data[k] = Number(data[k]);
      const res = await fetch('/api/settings', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body: JSON.stringify(data)
      });
      if(res.ok){
        document.getElementById('msg').innerHTML = 'Réglages sauvegardés !';
        // Rafraîchir le flux en changeant l'URL (pour forcer le navigateur à recharger)
        const img = document.getElementById('streamImg');
        img.src = '/stream?ts=' + Date.now();
      }else{
        document.getElementById('msg').innerHTML = 'Erreur lors de la sauvegarde.';
      }
    };
    loadSettings();
  </script>
</body>
</html>
)rawliteral";

// Handler GET /settings.html
static esp_err_t settings_html_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, settings_html, strlen(settings_html));
  return ESP_OK;
}

// Handler GET/POST /api/settings
static esp_err_t settings_api_handler(httpd_req_t *req)
{
  if (req->method == HTTP_GET)
  {
    StaticJsonDocument<256> doc;
    loadConfig(doc, "/settings.json");
    String out;
    serializeJson(doc, out);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, out.c_str());
    return ESP_OK;
  }
  else if (req->method == HTTP_POST)
  {
    char buf[256];
    int len = req->content_len;
    if (len > 255)
      len = 255;
    int r = httpd_req_recv(req, buf, len);
    if (r <= 0)
    {
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Reception failed");
      return ESP_FAIL;
    }
    buf[r] = 0;
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err)
    {
      httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalide");
      return ESP_FAIL;
    }
    saveConfig(doc, "/settings.json");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    // Appliquer les réglages à la caméra immédiatement
    sensor_t *s = esp_camera_sensor_get();
    if (s)
    {
      if (doc.containsKey("quality"))
        s->set_quality(s, doc["quality"]);
      if (doc.containsKey("contrast"))
        s->set_contrast(s, doc["contrast"]);
      if (doc.containsKey("brightness"))
        s->set_brightness(s, doc["brightness"]);
      if (doc.containsKey("saturation"))
        s->set_saturation(s, doc["saturation"]);
      if (doc.containsKey("awb"))
        s->set_whitebal(s, doc["awb"]);
      if (doc.containsKey("agc"))
        s->set_gain_ctrl(s, doc["agc"]);
      if (doc.containsKey("aec"))
        s->set_exposure_ctrl(s, doc["aec"]);
    }
    return ESP_OK;
  }
  httpd_resp_send_err(req, HTTPD_405_METHOD_NOT_ALLOWED, "Method not allowed");
  return ESP_FAIL;
}

// Page HTML de configuration WiFi
static const char config_html[] = R"rawliteral(
<!DOCTYPE html>
<html lang='fr'>
<head>
  <meta charset='UTF-8'>
  <title>Configuration WiFi</title>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <style>body{font-family:sans-serif;max-width:400px;margin:2em auto;}input,button{font-size:1em;margin:0.5em 0;width:100%;}</style>
</head>
<body>
  <h2>Configurer le WiFi</h2>
  <form id='wifiForm'>
    <label>SSID:<input name='ssid' required></label><br>
    <label>Mot de passe:<input name='password' type='password'></label><br>
    <button type='submit'>Enregistrer</button>
  </form>
  <button id='rebootBtn' style='background:#c00;color:#fff;'>Redémarrer l\'ESP32</button>
  <div id='msg'></div>
  <script>
    const form = document.getElementById('wifiForm');
    form.onsubmit = async e => {
      e.preventDefault();
      const data = {ssid:form.ssid.value,password:form.password.value};
      const res = await fetch('/api/config', {
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body: JSON.stringify(data)
      });
      if(res.ok){
        document.getElementById('msg').innerHTML = 'Configuration enregistrée. Redémarrez l\'ESP32.';
      }else{
        document.getElementById('msg').innerHTML = 'Erreur lors de l\'enregistrement.';
      }
    };
    document.getElementById('rebootBtn').onclick = async () => {
      if(confirm('Redémarrer l\\'ESP32 ?')){
        const res = await fetch('/api/reboot', {method:'POST'});
        if(res.ok){
          document.getElementById('msg').innerHTML = 'Redémarrage en cours...';
        }else{
          document.getElementById('msg').innerHTML = 'Erreur lors du redémarrage.';
        }
      }
    };
  </script>
  </body>
</html>
  )rawliteral";
// Handler POST /api/reboot
static esp_err_t reboot_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}");
  delay(200);
  ESP.restart();
  return ESP_OK;
}

// Handler GET /config.html
static esp_err_t config_html_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_send(req, config_html, strlen(config_html));
  return ESP_OK;
}
// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// Handler pour POST /api/config
static esp_err_t config_update_handler(httpd_req_t *req)
{
  // Limite de 1024 octets pour la config
  char buf[1025];
  int total_len = req->content_len;
  if (total_len > 1024)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload trop gros");
    return ESP_FAIL;
  }
  int ret = httpd_req_recv(req, buf, total_len);
  if (ret <= 0)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Lecture échouée");
    return ESP_FAIL;
  }
  buf[ret] = 0;

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, buf);
  if (error)
  {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalide");
    return ESP_FAIL;
  }
  if (!saveConfig(doc))
  {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Ecriture de la config échouée");
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}
// Enregistrement du endpoint config
httpd_uri_t config_uri = {
    .uri = "/api/config",
    .method = HTTP_POST,
    .handler = config_update_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = false,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
};

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// LED FLASH setup
#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255

int led_duty = 0;
bool isStreaming = false;

#endif

typedef struct
{
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

typedef struct
{
  size_t size;  // number of values used for filtering
  size_t index; // current value index
  size_t count; // value count
  int sum;
  int *values; // array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size)
{
  memset(filter, 0, sizeof(ra_filter_t));

  filter->values = (int *)malloc(sample_size * sizeof(int));
  if (!filter->values)
  {
    return NULL;
  }
  memset(filter->values, 0, sample_size * sizeof(int));

  filter->size = sample_size;
  return filter;
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static int ra_filter_run(ra_filter_t *filter, int value)
{
  if (!filter->values)
  {
    return value;
  }
  filter->sum -= filter->values[filter->index];
  filter->values[filter->index] = value;
  filter->sum += filter->values[filter->index];
  filter->index++;
  filter->index = filter->index % filter->size;
  if (filter->count < filter->size)
  {
    filter->count++;
  }
  return filter->sum / filter->count;
}
#endif

#if defined(LED_GPIO_NUM)
void enable_led(bool en)
{ // Turn LED On or Off
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY))
  {
    duty = CONFIG_LED_MAX_INTENSITY;
  }
  ledcWrite(LED_GPIO_NUM, duty);
  // ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL, duty);
  // ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL);
  log_i("Set LED intensity to %d", duty);
}
#endif

static esp_err_t bmp_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_start = esp_timer_get_time();
#endif
  fb = esp_camera_fb_get();
  if (!fb)
  {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/x-windows-bmp");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

  uint8_t *buf = NULL;
  size_t buf_len = 0;
  bool converted = frame2bmp(fb, &buf, &buf_len);
  esp_camera_fb_return(fb);
  if (!converted)
  {
    log_e("BMP Conversion failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  res = httpd_resp_send(req, (const char *)buf, buf_len);
  free(buf);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_end = esp_timer_get_time();
#endif
  log_i("BMP: %llums, %uB", (uint64_t)((fr_end - fr_start) / 1000), buf_len);
  return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len)
{
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index)
  {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK)
  {
    return 0;
  }
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_start = esp_timer_get_time();
#endif

#if defined(LED_GPIO_NUM)
  enable_led(true);
  vTaskDelay(150 / portTICK_PERIOD_MS); // The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
  fb = esp_camera_fb_get();             // or it won't be visible in the frame. A better way to do this is needed.
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif

  if (!fb)
  {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  size_t fb_len = 0;
#endif
  if (fb->format == PIXFORMAT_JPEG)
  {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = fb->len;
#endif
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  }
  else
  {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = jchunk.len;
#endif
  }
  esp_camera_fb_return(fb);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_end = esp_timer_get_time();
#endif
  log_i("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req)
{
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];

  static int64_t last_frame = 0;
  if (!last_frame)
  {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK)
  {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

#if defined(LED_GPIO_NUM)
  isStreaming = true;
  enable_led(true);
#endif

  while (true)
  {
    fb = esp_camera_fb_get();
    if (!fb)
    {
      log_e("Camera capture failed");
      res = ESP_FAIL;
    }
    else
    {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      if (fb->format != PIXFORMAT_JPEG)
      {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted)
        {
          log_e("JPEG compression failed");
          res = ESP_FAIL;
        }
      }
      else
      {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK)
    {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK)
    {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK)
    {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (fb)
    {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    }
    else if (_jpg_buf)
    {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK)
    {
      log_e("Send frame failed");
      break;
    }
    int64_t fr_end = esp_timer_get_time();

    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;

    frame_time /= 1000;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
#endif
    log_i(
        "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time,
        1000.0 / avg_frame_time);
  }

#if defined(LED_GPIO_NUM)
  isStreaming = false;
  enable_led(false);
#endif

  return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf)
{
  char *buf = NULL;
  size_t buf_len = 0;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1)
  {
    buf = (char *)malloc(buf_len);
    if (!buf)
    {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
    {
      *obuf = buf;
      return ESP_OK;
    }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
  Serial.printf("[cmd_handler] req ptr: %p\n", req);
  Serial.printf("[cmd_handler] req->uri: %s\n", req->uri ? req->uri : "(null)");
  Serial.printf("[cmd_handler] req->method: %d\n", req->method);
  Serial.printf("[cmd_handler] req->content_len: %d\n", req->content_len);

  char *buf = NULL;
  char variable[32];
  char value[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);
  // Affiche la commande reçue sur le Serial Monitor
  Serial.print("[cmd_uri] Commande reçue: var=");
  Serial.print(variable);
  Serial.print(", val=");
  Serial.println(value);

  int val = atoi(value);
  log_i("%s = %d", variable, val);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize"))
  {
    if (s->pixformat == PIXFORMAT_JPEG)
    {
      res = s->set_framesize(s, (framesize_t)val);
    }
  }
  else if (!strcmp(variable, "quality"))
  {
    res = s->set_quality(s, val);
  }
  else if (!strcmp(variable, "contrast"))
  {
    res = s->set_contrast(s, val);
  }
  else if (!strcmp(variable, "brightness"))
  {
    res = s->set_brightness(s, val);
  }
  else if (!strcmp(variable, "saturation"))
  {
    res = s->set_saturation(s, val);
  }
  else if (!strcmp(variable, "gainceiling"))
  {
    res = s->set_gainceiling(s, (gainceiling_t)val);
  }
  else if (!strcmp(variable, "colorbar"))
  {
    res = s->set_colorbar(s, val);
  }
  else if (!strcmp(variable, "awb"))
  {
    res = s->set_whitebal(s, val);
  }
  else if (!strcmp(variable, "agc"))
  {
    res = s->set_gain_ctrl(s, val);
  }
  else if (!strcmp(variable, "aec"))
  {
    res = s->set_exposure_ctrl(s, val);
  }
  else if (!strcmp(variable, "hmirror"))
  {
    res = s->set_hmirror(s, val);
  }
  else if (!strcmp(variable, "vflip"))
  {
    res = s->set_vflip(s, val);
  }
  else if (!strcmp(variable, "awb_gain"))
  {
    res = s->set_awb_gain(s, val);
  }
  else if (!strcmp(variable, "agc_gain"))
  {
    res = s->set_agc_gain(s, val);
  }
  else if (!strcmp(variable, "aec_value"))
  {
    res = s->set_aec_value(s, val);
  }
  else if (!strcmp(variable, "aec2"))
  {
    res = s->set_aec2(s, val);
  }
  else if (!strcmp(variable, "dcw"))
  {
    res = s->set_dcw(s, val);
  }
  else if (!strcmp(variable, "bpc"))
  {
    res = s->set_bpc(s, val);
  }
  else if (!strcmp(variable, "wpc"))
  {
    res = s->set_wpc(s, val);
  }
  else if (!strcmp(variable, "raw_gma"))
  {
    res = s->set_raw_gma(s, val);
  }
  else if (!strcmp(variable, "lenc"))
  {
    res = s->set_lenc(s, val);
  }
  else if (!strcmp(variable, "special_effect"))
  {
    res = s->set_special_effect(s, val);
  }
  else if (!strcmp(variable, "wb_mode"))
  {
    res = s->set_wb_mode(s, val);
  }
  else if (!strcmp(variable, "ae_level"))
  {
    res = s->set_ae_level(s, val);
  }
#if defined(LED_GPIO_NUM)
  else if (!strcmp(variable, "led_intensity"))
  {
    led_duty = val;
    if (isStreaming)
    {
      enable_led(true);
    }
  }
#endif
  else
  {
    log_i("Unknown command: %s", variable);
    res = -1;
  }

  if (res < 0)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static int print_reg(char *p, sensor_t *s, uint16_t reg, uint32_t mask)
{
  return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

static esp_err_t status_handler(httpd_req_t *req)
{
  static char json_response[1024];
  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';
  int first = 1;

// Ajout des registres (optionnel, peut être commenté si non utile)
/*
if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
  for (int reg = 0x3400; reg < 0x3406; reg += 2) {
    if (!first) p += sprintf(p, ","); first = 0;
    p += print_reg(p, s, reg, 0xFFF);
  }
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x3406, 0xFF);
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x3500, 0xFFFF0);
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x3503, 0xFF);
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x350a, 0x3FF);
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x350c, 0xFFFF);
  for (int reg = 0x5480; reg <= 0x5490; reg++) {
    if (!first) p += sprintf(p, ","); first = 0;
    p += print_reg(p, s, reg, 0xFF);
  }
  for (int reg = 0x5380; reg <= 0x538b; reg++) {
    if (!first) p += sprintf(p, ","); first = 0;
    p += print_reg(p, s, reg, 0xFF);
  }
  for (int reg = 0x5580; reg < 0x558a; reg++) {
    if (!first) p += sprintf(p, ","); first = 0;
    p += print_reg(p, s, reg, 0xFF);
  }
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x558a, 0x1FF);
} else if (s->id.PID == OV2640_PID) {
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0xd3, 0xFF);
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x111, 0xFF);
  if (!first) p += sprintf(p, ","); first = 0;
  p += print_reg(p, s, 0x132, 0xFF);
}
*/

// Ajout des champs principaux
#define ADD_FIELD(fmt, ...)            \
  do                                   \
  {                                    \
    if (!first)                        \
      p += sprintf(p, ",");            \
    p += sprintf(p, fmt, __VA_ARGS__); \
    first = 0;                         \
  } while (0)
  ADD_FIELD("\"xclk\":%u", s->xclk_freq_hz / 1000000);
  ADD_FIELD("\"pixformat\":%u", s->pixformat);
  ADD_FIELD("\"framesize\":%u", s->status.framesize);
  ADD_FIELD("\"quality\":%u", s->status.quality);
  ADD_FIELD("\"brightness\":%d", s->status.brightness);
  ADD_FIELD("\"contrast\":%d", s->status.contrast);
  ADD_FIELD("\"saturation\":%d", s->status.saturation);
  ADD_FIELD("\"sharpness\":%d", s->status.sharpness);
  ADD_FIELD("\"special_effect\":%u", s->status.special_effect);
  ADD_FIELD("\"wb_mode\":%u", s->status.wb_mode);
  ADD_FIELD("\"awb\":%u", s->status.awb);
  ADD_FIELD("\"awb_gain\":%u", s->status.awb_gain);
  ADD_FIELD("\"aec\":%u", s->status.aec);
  ADD_FIELD("\"aec2\":%u", s->status.aec2);
  ADD_FIELD("\"ae_level\":%d", s->status.ae_level);
  ADD_FIELD("\"aec_value\":%u", s->status.aec_value);
  ADD_FIELD("\"agc\":%u", s->status.agc);
  ADD_FIELD("\"agc_gain\":%u", s->status.agc_gain);
  ADD_FIELD("\"gainceiling\":%u", s->status.gainceiling);
  ADD_FIELD("\"bpc\":%u", s->status.bpc);
  ADD_FIELD("\"wpc\":%u", s->status.wpc);
  ADD_FIELD("\"raw_gma\":%u", s->status.raw_gma);
  ADD_FIELD("\"lenc\":%u", s->status.lenc);
  ADD_FIELD("\"hmirror\":%u", s->status.hmirror);
  ADD_FIELD("\"vflip\":%u", s->status.vflip);
  ADD_FIELD("\"dcw\":%u", s->status.dcw);
  ADD_FIELD("\"colorbar\":%u", s->status.colorbar);
#if defined(LED_GPIO_NUM)
  ADD_FIELD("\"led_intensity\":%u", led_duty);
#else
  ADD_FIELD("\"led_intensity\":%d", -1);
#endif
#undef ADD_FIELD
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t xclk_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char _xclk[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int xclk = atoi(_xclk);
  log_i("Set XCLK: %d MHz", xclk);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t reg_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char _reg[32];
  char _mask[32];
  char _val[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK || httpd_query_key_value(buf, "val", _val, sizeof(_val)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  int val = atoi(_val);
  log_i("Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_reg(s, reg, mask, val);
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t greg_handler(httpd_req_t *req)
{
  char *buf = NULL;
  char _reg[32];
  char _mask[32];

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK)
  {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->get_reg(s, reg, mask);
  if (res < 0)
  {
    return httpd_resp_send_500(req);
  }
  log_i("Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);

  char buffer[20];
  const char *val = itoa(res, buffer, 10);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, val, strlen(val));
}

static int parse_get_var(char *buf, const char *key, int def)
{
  char _int[16];
  if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK)
  {
    return def;
  }
  return atoi(_int);
}

static esp_err_t pll_handler(httpd_req_t *req)
{
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }

  int bypass = parse_get_var(buf, "bypass", 0);
  int mul = parse_get_var(buf, "mul", 0);
  int sys = parse_get_var(buf, "sys", 0);
  int root = parse_get_var(buf, "root", 0);
  int pre = parse_get_var(buf, "pre", 0);
  int seld5 = parse_get_var(buf, "seld5", 0);
  int pclken = parse_get_var(buf, "pclken", 0);
  int pclk = parse_get_var(buf, "pclk", 0);
  free(buf);

  log_i("Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t win_handler(httpd_req_t *req)
{
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK)
  {
    return ESP_FAIL;
  }

  int startX = parse_get_var(buf, "sx", 0);
  int startY = parse_get_var(buf, "sy", 0);
  int endX = parse_get_var(buf, "ex", 0);
  int endY = parse_get_var(buf, "ey", 0);
  int offsetX = parse_get_var(buf, "offx", 0);
  int offsetY = parse_get_var(buf, "offy", 0);
  int totalX = parse_get_var(buf, "tx", 0);
  int totalY = parse_get_var(buf, "ty", 0); // codespell:ignore totaly
  int outputX = parse_get_var(buf, "ox", 0);
  int outputY = parse_get_var(buf, "oy", 0);
  bool scale = parse_get_var(buf, "scale", 0) == 1;
  bool binning = parse_get_var(buf, "binning", 0) == 1;
  free(buf);

  log_i(
      "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY,
      totalX, totalY, outputX, outputY, scale, binning // codespell:ignore totaly
  );
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning); // codespell:ignore totaly
  if (res)
  {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t index_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL)
  {
    if (s->id.PID == OV3660_PID)
    {
      return httpd_resp_send(req, (const char *)index_ov3660_html_gz, index_ov3660_html_gz_len);
    }
    else if (s->id.PID == OV5640_PID)
    {
      return httpd_resp_send(req, (const char *)index_ov5640_html_gz, index_ov5640_html_gz_len);
    }
    else
    {
      return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
    }
  }
  else
  {
    log_e("Camera sensor not found");
    return httpd_resp_send_500(req);
  }
}

void startCameraServer()
{
  Serial.println("startCameraServer() Starting web server on port: '80'");
  httpd_uri_t settings_html_uri = {
      .uri = "/settings.html",
      .method = HTTP_GET,
      .handler = settings_html_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };
  httpd_uri_t settings_api_uri = {
      .uri = "/api/settings",
      .method = HTTP_GET,
      .handler = settings_api_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };
  httpd_uri_t settings_api_post_uri = {
      .uri = "/api/settings",
      .method = HTTP_POST,
      .handler = settings_api_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };
  httpd_uri_t reboot_uri = {
      .uri = "/api/reboot",
      .method = HTTP_POST,
      .handler = reboot_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t config_html_uri = {
      .uri = "/config.html",
      .method = HTTP_GET,
      .handler = config_html_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = false,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 24;
  config.max_open_sockets = 4; // Augmente à 4 connexions simultanées (adapte selon ta RAM)

  httpd_uri_t index_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = index_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t status_uri = {
      .uri = "/status",
      .method = HTTP_GET,
      .handler = status_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t cmd_uri = {
      .uri = "/control",
      .method = HTTP_GET,
      .handler = cmd_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t capture_uri = {
      .uri = "/capture",
      .method = HTTP_GET,
      .handler = capture_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t stream_uri = {
      .uri = "/stream",
      .method = HTTP_GET,
      .handler = stream_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t bmp_uri = {
      .uri = "/bmp",
      .method = HTTP_GET,
      .handler = bmp_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t xclk_uri = {
      .uri = "/xclk",
      .method = HTTP_GET,
      .handler = xclk_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t reg_uri = {
      .uri = "/reg",
      .method = HTTP_GET,
      .handler = reg_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t greg_uri = {
      .uri = "/greg",
      .method = HTTP_GET,
      .handler = greg_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t pll_uri = {
      .uri = "/pll",
      .method = HTTP_GET,
      .handler = pll_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t win_uri = {
      .uri = "/resolution",
      .method = HTTP_GET,
      .handler = win_handler,
      .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
      ,
      .is_websocket = true,
      .handle_ws_control_frames = false,
      .supported_subprotocol = NULL
#endif
  };

  ra_filter_init(&ra_filter, 20);

  log_i("Starting web server on port: '%d'", config.server_port);
  Serial.println("[DIAG] Registering URI handlers...");
  if (httpd_start(&camera_httpd, &config) == ESP_OK)
  {
    Serial.println("[DIAG] Web server started OK");
    Serial.println("[DIAG] Registering /control handler...");
    esp_err_t reg_res = httpd_register_uri_handler(camera_httpd, &cmd_uri);
    if (reg_res == ESP_OK)
    {
      Serial.println("[DIAG] /control handler registered successfully");
    }
    else
    {
      Serial.printf("[DIAG] /control handler registration FAILED: %d\n", reg_res);
    }
    // Register other handlers as before
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &bmp_uri);
    httpd_register_uri_handler(camera_httpd, &xclk_uri);
    httpd_register_uri_handler(camera_httpd, &reg_uri);
    httpd_register_uri_handler(camera_httpd, &greg_uri);
    httpd_register_uri_handler(camera_httpd, &pll_uri);
    httpd_register_uri_handler(camera_httpd, &win_uri);
    httpd_register_uri_handler(camera_httpd, &config_uri);
    httpd_register_uri_handler(camera_httpd, &config_html_uri);
    httpd_register_uri_handler(camera_httpd, &reboot_uri);
    httpd_register_uri_handler(camera_httpd, &settings_html_uri);
    httpd_register_uri_handler(camera_httpd, &settings_api_uri);
    httpd_register_uri_handler(camera_httpd, &settings_api_post_uri);
    // Charger et appliquer les réglages caméra au démarrage
    StaticJsonDocument<512> doc_settings;

    if (loadConfig(doc_settings, "/settings.json"))
    {
      Serial.println("[DIAG] LoadConfig OK");
      sensor_t *s = esp_camera_sensor_get();
      if (s)
      {
        if (doc_settings.containsKey("quality"))
          s->set_quality(s, doc_settings["quality"]);
        if (doc_settings.containsKey("contrast"))
          s->set_contrast(s, doc_settings["contrast"]);
        if (doc_settings.containsKey("brightness"))
          s->set_brightness(s, doc_settings["brightness"]);
        if (doc_settings.containsKey("saturation"))
          s->set_saturation(s, doc_settings["saturation"]);
        if (doc_settings.containsKey("awb"))
          s->set_whitebal(s, doc_settings["awb"]);
        if (doc_settings.containsKey("agc"))
          s->set_gain_ctrl(s, doc_settings["agc"]);
        if (doc_settings.containsKey("aec"))
          s->set_exposure_ctrl(s, doc_settings["aec"]);
      }
      else
      {
        Serial.println("[DIAG] esp_camera_sensor_get Error");
      }
    }
    else
    {
      Serial.println("[DIAG] LoadConfig Error");
    }
  }
  else
  {
    Serial.println("[DIAG] Web server FAILED to start");
  }

  // Enregistrer le handler /stream sur le même serveur HTTP (port 80)
  Serial.println("[DIAG] Tentative d'enregistrement du handler /stream...");
  esp_err_t stream_reg_res = httpd_register_uri_handler(camera_httpd, &stream_uri);
  Serial.printf("[DIAG] Résultat httpd_register_uri_handler /stream: %d\n", stream_reg_res);
  if (stream_reg_res == ESP_OK)
  {
    Serial.println("[DIAG] Handler /stream enregistré avec succès sur le serveur principal");
  }
  else
  {
    Serial.printf("[DIAG] Échec de l'enregistrement du handler /stream sur le serveur principal: %d\n", stream_reg_res);
  }
}

void setupLedFlash()
{
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#else
  log_i("LED flash is disabled -> LED_GPIO_NUM undefined");
#endif
}
