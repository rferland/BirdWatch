#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>

// Utilitaire pour charger la config depuis LittleFS
bool loadConfig(JsonDocument &doc, const char *filename)
{
      if (!LittleFS.begin())
            return false;
      File file = LittleFS.open(filename, "r");
      if (!file)
            return false;
      DeserializationError error = deserializeJson(doc, file);
      file.close();
      return !error;
}

// Utilitaire pour sauvegarder la config dans LittleFS
bool saveConfig(const JsonDocument &doc, const char *filename)
{
      if (!LittleFS.begin())
            return false;
      File file = LittleFS.open(filename, "w");
      if (!file)
            return false;
      serializeJson(doc, file);
      file.close();
      return true;
}
