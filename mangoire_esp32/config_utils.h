#pragma once
#include <ArduinoJson.h>

// Déclarations des fonctions utilitaires de config
bool loadConfig(JsonDocument &doc, const char *filename = "/config.json");
bool saveConfig(const JsonDocument &doc, const char *filename = "/config.json");
