#include <Arduino.h>
#include <spiffs_api.h>
#include <EasyOTA.h>

// C99 libraries
#include <cstdlib>
#include <stdbool.h>
#include <string.h>
#include <time.h>

// Libraries for MQTT client, WiFi connection and SAS-token generation.
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <base64.h>
#include <bearssl/bearssl.h>
#include <bearssl/bearssl_hmac.h>
#include <libb64/cdecode.h>

// Azure IoT SDK for C includes
#include <az_core.h>
#include <az_iot.h>
#include <azure_ca.h>

// Additional sample headers
#include <config.h>

//
#include <Helpers.h>

// Sensors
#include <DHT_U.h>

// When developing for your own Arduino-based platform,
// please follow the format '(ard;<platform>)'.
#define AZURE_SDK_CLIENT_USER_AGENT "c%2F" AZ_SDK_VERSION_STRING "(ard;esp8266)"

// Utility macros and defines
#define LED_PIN LED_BUILTIN
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define ONE_HOUR_IN_SECS 3600
#define NTP_SERVERS "pool.ntp.org", "time.nist.gov"
#define MQTT_PACKET_SIZE 1024
#define MESSAGE_MAX_LEN 255

// Memory allocated for the sample's variables and structures.
static WiFiClientSecure wifi_client;
static WiFiClient wificlient;
static X509List cert((const char *)ca_pem);
static PubSubClient mqtt_client(wifi_client);
static az_iot_hub_client client;
static char sas_token[200];
static uint8_t signature[512];
static unsigned char encrypted_signature[32];
static char base64_decoded_device_key[32];
static char telemetry_topic[128];
// static uint32_t telemetry_send_count = 0;

const char *messageData = "{\"DeviceId\":\"%s\", \"Temperature\":%f, \"Humidity\":%f, \"Version\":\"" FW_VERSION "\"}";

// Translate iot_configs.h defines into variables used by the sample
static const char *host = IOTHOST;
static const char *device_id = IOTID;
static const char *device_key = IOTKEY;
static const int port = 8883;

// Sensor configuration
#define DHTPIN 2
#define DHTTYPE DHT11 // DHT 11
// #define DHTTYPE           DHT22     // DHT 22 (AM2302)
// #define DHTTYPE           DHT21     // DHT 21 (AM2301)

// See guide for details on sensor wiring and usage:
//   https://learn.adafruit.com/dht/overview

DHT_Unified dht(DHTPIN, DHTTYPE);

static void connectToWiFi()
{
  Helpers::SerialPrintf("Connecting to WIFI SSID %s", SSIDNAME);

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSIDNAME, SSIDKEY);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Helpers::SerialPrintf("WiFi connected, Signal strentgh: %d\n", WiFi.RSSI());
}

static void initializeTime()
{
  Helpers::SerialPrintf("Setting time using SNTP");

  configTime(-5 * 3600, 0, NTP_SERVERS);
  time_t now = time(NULL);
  while (now < 1510592825)
  {
    delay(500);
    Serial.print(".");
    now = time(NULL);
  }
  Helpers::SerialPrintln("done!");
}

static char *getCurrentLocalTimeString()
{
  time_t now = time(NULL);
  return ctime(&now);
}

static void printCurrentTime()
{
  Helpers::SerialPrintf("Current time: ");
  Helpers::SerialPrintln(getCurrentLocalTimeString());
}

void receivedCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println("");
}

static void initializeClients()
{
  az_iot_hub_client_options options = az_iot_hub_client_options_default();
  options.user_agent = AZ_SPAN_FROM_STR(AZURE_SDK_CLIENT_USER_AGENT);

  wifi_client.setTrustAnchors(&cert);
  if (az_result_failed(az_iot_hub_client_init(
          &client,
          az_span_create((uint8_t *)host, strlen(host)),
          az_span_create((uint8_t *)device_id, strlen(device_id)),
          &options)))
  {
    Helpers::SerialPrintln("Failed initializing Azure IoT Hub client");
    return;
  }
  Helpers::SerialPrintf("IoT server set for %s, %d\n", host, port);
  mqtt_client.setServer(host, port);
  mqtt_client.setCallback(receivedCallback);
}

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t getSecondsSinceEpoch() { return (uint32_t)time(NULL); }

static int generateSasToken(char *sas_token, size_t size)
{
  az_span signature_span = az_span_create((uint8_t *)signature, sizeofarray(signature));
  az_span out_signature_span;
  az_span encrypted_signature_span = az_span_create((uint8_t *)encrypted_signature, sizeofarray(encrypted_signature));

  uint32_t expiration = getSecondsSinceEpoch() + ONE_HOUR_IN_SECS;

  // Get signature
  if (az_result_failed(az_iot_hub_client_sas_get_signature(
          &client, expiration, signature_span, &out_signature_span)))
  {
    Helpers::SerialPrintln("Failed getting SAS signature");
    return 1;
  }

  // Base64-decode device key
  int base64_decoded_device_key_length = base64_decode_chars(device_key, strlen(device_key), base64_decoded_device_key);

  if (base64_decoded_device_key_length == 0)
  {
    Helpers::SerialPrintln("Failed base64 decoding device key");
    return 1;
  }

  // SHA-256 encrypt
  br_hmac_key_context kc;
  br_hmac_key_init(
      &kc, &br_sha256_vtable, base64_decoded_device_key, base64_decoded_device_key_length);

  br_hmac_context hmac_ctx;
  br_hmac_init(&hmac_ctx, &kc, 32);
  br_hmac_update(&hmac_ctx, az_span_ptr(out_signature_span), az_span_size(out_signature_span));
  br_hmac_out(&hmac_ctx, encrypted_signature);

  // Base64 encode encrypted signature
  String b64enc_hmacsha256_signature = base64::encode(encrypted_signature, br_hmac_size(&hmac_ctx));

  az_span b64enc_hmacsha256_signature_span = az_span_create(
      (uint8_t *)b64enc_hmacsha256_signature.c_str(), b64enc_hmacsha256_signature.length());

  // URl-encode base64 encoded encrypted signature
  if (az_result_failed(az_iot_hub_client_sas_get_password(
          &client,
          expiration,
          b64enc_hmacsha256_signature_span,
          AZ_SPAN_EMPTY,
          sas_token,
          size,
          NULL)))
  {
    Helpers::SerialPrintln("Failed getting SAS token");
    return 1;
  }

  return 0;
}

static int connectToAzureIoTHub()
{
  size_t client_id_length;
  char mqtt_client_id[128];
  if (az_result_failed(az_iot_hub_client_get_client_id(
          &client, mqtt_client_id, sizeof(mqtt_client_id) - 1, &client_id_length)))
  {
    Helpers::SerialPrintln("Failed getting client id");
    return 1;
  }

  mqtt_client_id[client_id_length] = '\0';

  char mqtt_username[128];
  // Get the MQTT user name used to connect to IoT Hub
  if (az_result_failed(az_iot_hub_client_get_user_name(
          &client, mqtt_username, sizeofarray(mqtt_username), NULL)))
  {
    Helpers::SerialPrintln("Failed to get MQTT clientId, return code\n");
    return 1;
  }

  Helpers::SerialPrintf("Client ID: ");
  Helpers::SerialPrintln(mqtt_client_id);

  Helpers::SerialPrintf("Username: ");
  Helpers::SerialPrintln(mqtt_username);

  mqtt_client.setBufferSize(MQTT_PACKET_SIZE);

  while (!mqtt_client.connected())
  {
    time_t now = time(NULL);

    Helpers::SerialPrintln("MQTT connecting ... ");

    if (mqtt_client.connect(mqtt_client_id, mqtt_username, sas_token))
    {
      Helpers::SerialPrintln("connected.");
    }
    else
    {
      Helpers::SerialPrintf("failed, status code =");
      Serial.println(mqtt_client.state());
      Helpers::SerialPrintln(". Trying again in 5 seconds.");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

  mqtt_client.subscribe(AZ_IOT_HUB_CLIENT_C2D_SUBSCRIBE_TOPIC);

  return 0;
}

static void establishConnection()
{
  Serial.begin(115200);
  connectToWiFi();
  initializeTime();
  printCurrentTime();
  initializeClients();

  // The SAS token is valid for 1 hour by default in this sample.
  // After one hour the sample must be restarted, or the client won't be able
  // to connect/stay connected to the Azure IoT Hub.
  if (generateSasToken(sas_token, sizeofarray(sas_token)) != 0)
  {
    Helpers::SerialPrintln("Failed generating MQTT password");
  }
  else
  {
    connectToAzureIoTHub();
  }

  digitalWrite(LED_PIN, LOW);
}

static void sendTelemetry()
{
  digitalWrite(LED_PIN, HIGH);
  Serial.println(millis());
  Helpers::SerialPrintln(" ESP8266 Sending telemetry . . . ");
  if (az_result_failed(az_iot_hub_client_telemetry_get_publish_topic(
          &client, NULL, telemetry_topic, sizeof(telemetry_topic), NULL)))
  {
    Helpers::SerialPrintln("Failed az_iot_hub_client_telemetry_get_publish_topic");
    return;
  }

  // Sensors

  Helpers::SerialPrintln("Init sensors... ");
  //

  float temperature = 0;
  float humidity = 0;
  dht.begin();

  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature))
    Helpers::SerialPrintln("Error reading temperature!");
  else
    temperature = event.temperature;

  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity))
    Helpers::SerialPrintln("Error reading humidity!");
  else
    humidity = event.relative_humidity;
  //

  char messagePayload[MESSAGE_MAX_LEN];
  snprintf(messagePayload, MESSAGE_MAX_LEN, messageData, device_id, temperature, humidity);
  Helpers::SerialPrintln(messagePayload);

#ifndef debugon
  mqtt_client.publish(telemetry_topic, messagePayload, false);
  Helpers::SerialPrintln("Payload sent.");
#endif

  delay(100);
  digitalWrite(LED_PIN, LOW);
}

void setup()
{
  Serial.begin(115200);
  Helpers::SerialPrintf("Waking up... %s build %s\n", IOTID, FW_VERSION);
  // Initializing SPIFFS
  Helpers::SerialPrintln("SPIFFS init...");
  SPIFFSConfig cfg;
  cfg.setAutoFormat(true);
  SPIFFS.setConfig(cfg);
  if (SPIFFS.begin())
    Serial.println("SPIFFS mounted successfully");

  establishConnection();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  // OTA update
  Helpers::SerialPrintf("Checking for updates on %s\n", OTA_GET_VERSION_ENDPOINT IOTID);
  OTAConfig config(
      FW_VERSION,
      OTA_SERVER_URL,
      OTA_GET_VERSION_ENDPOINT IOTID,
      OTA_GET_FIRMWARE_ENDPOINT IOTID,
      true);
  EasyOTA ota(config);
  ota.runUpdateRoutine(wificlient);

  Helpers::SerialPrintln("Sending telemetry...");
  sendTelemetry();
  mqtt_client.loop();
  Helpers::Blink(3, 1000, 500);
  Helpers::Uploadlog(wificlient);
  Helpers::Sleep();
  ESP.restart();
}

void loop()
{
  // put your main code here, to run repeatedly:
}