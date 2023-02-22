#ifndef Helpers_cpp_
#define Helpers_cpp_

#include <Arduino.h>
#include <Wire.h>
#include <Helpers.h>
#include <spiffs_api.h>
#include <ESP8266HTTPClient.h>
// Config
#include <config.h>

#define uS_TO_S_FACTOR 1000000
#define logFile "seriallog.txt"

void Helpers::Sleep()
{
  SerialPrintln("Going to sleep...");
  // esp_sleep_enable_timer_wakeup(TimeToSleepInSeconds * uS_TO_S_FACTOR);
  // esp_deep_sleep_start();
}
int Helpers::SerialPrintln(const char *format)
{
  int r = Helpers::SerialPrintf(format);
  Helpers::SerialPrintf("\n");
  return r + 1;
}
int Helpers::SerialPrintf(const char *format, ...)
{
  File log;
  if (SPIFFS.exists("/" logFile))
    log = SPIFFS.open("/" logFile, "a");
  else
    log = SPIFFS.open("/" logFile, "w");

  int ret;
  va_list arg;
  char loc_buf[128];
  char *temp = loc_buf;
  va_start(arg, format);
  ret = vsnprintf(temp, sizeof(loc_buf), format, arg);
  va_end(arg);
  log.print(temp);
  ret = Serial.print(temp);

  if (temp != loc_buf)
  {
    free(temp);
  }
  log.close();
  return ret;
}

void Helpers::Readlog()
{
  File fileToRead = SPIFFS.open("/" logFile, "r");

  if (!fileToRead)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.println("File Content:");

  while (fileToRead.available())
  {

    Serial.write(fileToRead.read());
  }

  fileToRead.close();
}

void Helpers::Blink(int num, int on, int off)
{
  for (int i = 0; i < num; i++)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(on);
    digitalWrite(LED_BUILTIN, LOW);
    delay(off);
  }
}

void Helpers::Uploadlog(WiFiClient &client)
{
  HTTPClient http;
  File fileToRead = SPIFFS.open("/" logFile, "r");

  if (!fileToRead)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  if (!http.begin(client, OTA_SERVER_URL LOG_UPLOAD_ENDPOINT IOTID))
  {
    SerialPrintln("Connection to http failed.");
    return;
  }
  int httpCode = http.POST(fileToRead.readString());
  fileToRead.close();
  if (httpCode == 200)
  {
    SerialPrintln("Log file uploaded, removing it from the device.");
    SPIFFS.remove("/" logFile);
  }
}

#endif // Helpers_cpp_