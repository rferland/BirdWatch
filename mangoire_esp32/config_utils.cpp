#include <ArduinoJson.h>
#include <FS.h>
#include <LittleFS.h>
#include <arduino.h>
// Utilitaire pour charger la config depuis LittleFS
bool loadConfig(JsonDocument &doc, const char *filename)
{
      Serial.printf("[DIAG] Chargement de la config depuis %s\n", filename);
      if (!LittleFS.begin())
            return false;
      File file = LittleFS.open(filename, "r");
      if (!file)
            return false;
      DeserializationError error = deserializeJson(doc, file);
      file.close();
      Serial.println("[DIAG] Chargement de la config terminé");
      return !error;
}

// Utilitaire pour sauvegarder la config dans LittleFS
bool saveConfig(const JsonDocument &doc, const char *filename)
{
      Serial.printf("[DIAG] Sauvegarde de la config dans %s\n", filename);
      if (!LittleFS.begin())
            return false;
      File file = LittleFS.open(filename, "w");
      if (!file)
            return false;
      serializeJson(doc, file);
      file.close();
      Serial.println("[DIAG] Config sauvegardée avec succès");
      return true;
}
