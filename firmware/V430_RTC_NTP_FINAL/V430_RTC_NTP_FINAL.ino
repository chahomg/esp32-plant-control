#include <Arduino.h>
#include "plant_factory_config.h"
struct AuditDay426; // Forward declaration for Arduino IDE 1.8.x auto-generated prototypes
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <nvs.h>
#include <esp_err.h>
#include <ArduinoOTA.h>
#include <esp_system.h>
#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>
#include <esp_mac.h>
#include <mbedtls/md.h>
#include <string.h>


// Arduino Cloud A test: only serial ON/OFF, NO relay output.
// Fill these two fields LOCALLY using Arduino Cloud SketchSecrets.
// Never share the device secret in chat or screenshots.
const char* CLOUD_DEVICE_ID = "ENTER_CLOUD_DEVICE_ID";
const char* CLOUD_DEVICE_KEY = "ENTER_CLOUD_DEVICE_KEY";

extern float humidityRH;
extern float temperatureC;
bool plantLightSwitch = false;
WiFiConnectionHandler* cloudConnection = nullptr;
bool cloudStarted = false;

void onPlantLightSwitchChange() {
  Serial.print("CLOUD plantLightSwitch: ");
  Serial.println(plantLightSwitch ? "ON" : "OFF");
  // TEST ONLY: no GPIO or relay actions.
}

void beginPlantCloud(const String& ssid, const String& pass) {
  if (String(CLOUD_DEVICE_ID).startsWith("PASTE_") ||
      String(CLOUD_DEVICE_KEY).startsWith("PASTE_")) {
    Serial.println("CLOUD: credentials not configured; local functions remain available");
    return;
  }
  ArduinoCloud.setBoardId(CLOUD_DEVICE_ID);
  ArduinoCloud.setSecretDeviceKey(CLOUD_DEVICE_KEY);
  ArduinoCloud.addProperty(plantLightSwitch, READWRITE, ON_CHANGE, onPlantLightSwitchChange);
  ArduinoCloud.addProperty(humidityRH, READ, 60 * SECONDS, NULL);
  ArduinoCloud.addProperty(temperatureC, READ, 60 * SECONDS, NULL);
  // Arduino Cloud's handler manages its own network reconnection.
  // Existing Wi-Fi/OTA provisioning remains in the original sketch.
  cloudConnection = new WiFiConnectionHandler(ssid.c_str(), pass.c_str());
  ArduinoCloud.begin(*cloudConnection);
  cloudStarted = true;
  Serial.println("CLOUD: initialization requested");
}

Preferences prefs;
WebServer web(80);
DNSServer setupDNS;
const uint16_t SETUP_DNS_PORT = 53;

const char* SETUP_SSID = PLANT_FACTORY_SSID;
const int PLANT_BOOT_PIN = 0;
const unsigned long SETUP_LIMIT = 600000UL; // V4.29: 10-minute idle setup window
const char* FW_VERSION = "V4.30-RTC-NTP";
const int SHT_SDA = 8;
const int SHT_SCL = 9;
const uint8_t SHT_ADDRS[] = {0x44, 0x45};
uint8_t shtAddress = 0;
float temperatureC = NAN, humidityRH = NAN;
bool shtOK = false;
unsigned long lastShtRead = 0;
const char* OTA_HOSTNAME = "esp32-plant";
String otaPassword;
bool otaReady = false;

String wifiName, wifiPass;
// Factory pairing credential is compile-time fixed; never stored in user Wi-Fi NVS.
const char* setupPass = PLANT_FACTORY_PASSWORD;
bool setupMode = false;
bool changePending = false;
unsigned long setupStarted = 0;
unsigned long bootStarted = 0;
unsigned long changeAt = 0;
String newName, newPass;
// V4.29 provisioning result: queried by phone while still connected to setup AP.
String provisionState = "idle";
String provisionIP = "";
String provisionSSID = "";
String provisionMessage = "";
String provisionError = "";
String provisionStage = "idle";
unsigned long provisionSuccessAt = 0;
const unsigned long SETUP_SUCCESS_GRACE_MS = 30000UL; // V4.29: display success before closing AP
void touchSetup() { if (setupMode) setupStarted = millis(); }


// Firebase RTDB upload (device-only credentials; do not publish this sketch).
// Enter the Firebase Authentication email and password you created for the ESP32.
const char* FIREBASE_API_KEY = "ENTER_FIREBASE_API_KEY";
const char* FIREBASE_DEVICE_EMAIL = "ENTER_FIREBASE_DEVICE_EMAIL";
const char* FIREBASE_DEVICE_PASSWORD = "ENTER_FIREBASE_DEVICE_PASSWORD";
const char* FIREBASE_DB_HOST = "ENTER_FIREBASE_DB_HOST";
const char* FIREBASE_EXPECTED_UID = "ENTER_FIREBASE_EXPECTED_UID";
const unsigned long FIREBASE_UPLOAD_INTERVAL_MS = 30000UL;
const unsigned long FIREBASE_RETRY_INTERVAL_MS = 60000UL;
const unsigned long FIREBASE_RATE_LIMIT_WAIT_MS = 3600000UL;
const unsigned long FIREBASE_MAX_RETRY_INTERVAL_MS = 900000UL;
const unsigned long DIAG_PROBE_INTERVAL_MS = 120000UL;

// Trusted roots: GTS R1/R2, ISRG X1, GTS R3/R4.
// GTS R3/R4 SHA-256 fingerprints checked against https://pki.goog/repository/
// Multiple roots are needed because Google may serve different certificate chains.
static const char FIREBASE_ROOT_CAS[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlk28xsBNJiGuiFzANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjEwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjEwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQC2EQKLHuOhd5s73L+UPreVp0A8of2C+X0yBoJx9vaMf/vo
27xqLpeXo4xL+Sv2sfnOhB2x+cWX3u+58qPpvBKJXqeqUqv4IyfLpLGcY9vXmX7w
Cl7raKb0xlpHDU0QM+NOsROjyBhsS+z8CZDfnWQpJSMHobTSPS5g4M/SCYe7zUjw
TcLCeoiKu7rPWRnWr4+wB7CeMfGCwcDfLqZtbBkOtdh+JhpFAz2weaSUKK0Pfybl
qAj+lug8aJRT7oM6iCsVlgmy4HqMLnXWnOunVmSPlk9orj2XwoSPwLxAwAtcvfaH
szVsrBhQf4TgTM2S0yDpM7xSma8ytSmzJSq0SPly4cpk9+aCEI3oncKKiPo4Zor8
Y/kB+Xj9e1x3+naH+uzfsQ55lVe0vSbv1gHR6xYKu44LtcXFilWr06zqkUspzBmk
MiVOKvFlRNACzqrOSbTqn3yDsEB750Orp2yjj32JgfpMpf/VjsPOS+C12LOORc92
wO1AK/1TD7Cn1TsNsYqiA94xrcx36m97PtbfkSIS5r762DL8EGMUUXLeXdYWk70p
aDPvOmbsB4om3xPXV2V4J95eSRQAogB/mqghtqmxlbCluQ0WEdrHbEg8QOB+DVrN
VjzRlwW5y0vtOUucxD/SVRNuJLDWcfr0wbrM7Rv1/oFB2ACYPTrIrnqYNxgFlQID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQU5K8rJnEaK0gnhS9SZizv8IkTcT4wDQYJKoZIhvcNAQEMBQADggIBAJ+qQibb
C5u+/x6Wki4+omVKapi6Ist9wTrYggoGxval3sBOh2Z5ofmmWJyq+bXmYOfg6LEe
QkEzCzc9zolwFcq1JKjPa7XSQCGYzyI0zzvFIoTgxQ6KfF2I5DUkzps+GlQebtuy
h6f88/qBVRRiClmpIgUxPoLW7ttXNLwzldMXG+gnoot7TiYaelpkttGsN/H9oPM4
7HLwEXWdyzRSjeZ2axfG34arJ45JK3VmgRAhpuo+9K4l/3wV3s6MJT/KYnAK9y8J
ZgfIPxz88NtFMN9iiMG1D53Dn0reWVlHxYciNuaCp+0KueIHoI17eko8cdLiA6Ef
MgfdG+RCzgwARWGAtQsgWSl4vflVy2PFPEz0tv/bal8xa5meLMFrUKTX5hgUvYU/
Z6tGn6D/Qqc6f1zLXbBwHSs09dR2CQzreExZBfMzQsNhFRAbd03OIozUhfJFfbdT
6u9AWpQKXCBfTkBdYiJ23//OYb2MI3jSNwLgjt7RETeJ9r/tSQdirpLsQBqvFAnZ
0E6yove+7u7Y/9waLd64NnHi/Hm3lCXRSHNboTXns5lndcEZOitHTtNCjv0xyBZm
2tIMPNuzjsmhDYAPexZ3FL//2wmUspO8IFgV6dtxQ/PeEMMA3KgqlbbC1j+Qa3bb
bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFVzCCAz+gAwIBAgINAgPlrsWNBCUaqxElqjANBgkqhkiG9w0BAQwFADBHMQsw
CQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEU
MBIGA1UEAxMLR1RTIFJvb3QgUjIwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAw
MDAwWjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZp
Y2VzIExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjIwggIiMA0GCSqGSIb3DQEBAQUA
A4ICDwAwggIKAoICAQDO3v2m++zsFDQ8BwZabFn3GTXd98GdVarTzTukk3LvCvpt
nfbwhYBboUhSnznFt+4orO/LdmgUud+tAWyZH8QiHZ/+cnfgLFuv5AS/T3KgGjSY
6Dlo7JUle3ah5mm5hRm9iYz+re026nO8/4Piy33B0s5Ks40FnotJk9/BW9BuXvAu
MC6C/Pq8tBcKSOWIm8Wba96wyrQD8Nr0kLhlZPdcTK3ofmZemde4wj7I0BOdre7k
RXuJVfeKH2JShBKzwkCX44ofR5GmdFrS+LFjKBC4swm4VndAoiaYecb+3yXuPuWg
f9RhD1FLPD+M2uFwdNjCaKH5wQzpoeJ/u1U8dgbuak7MkogwTZq9TwtImoS1mKPV
+3PBV2HdKFZ1E66HjucMUQkQdYhMvI35ezzUIkgfKtzra7tEscszcTJGr61K8Yzo
dDqs5xoic4DSMPclQsciOzsSrZYuxsN2B6ogtzVJV+mSSeh2FnIxZyuWfoqjx5RW
Ir9qS34BIbIjMt/kmkRtWVtd9QCgHJvGeJeNkP+byKq0rxFROV7Z+2et1VsRnTKa
G73VululycslaVNVJ1zgyjbLiGH7HrfQy+4W+9OmTN6SpdTi3/UGVN4unUu0kzCq
gc7dGtxRcw1PcOnlthYhGXmy5okLdWTK1au8CcEYof/UVKGFPP0UJAOyh9OktwID
AQABo0IwQDAOBgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4E
FgQUu//KjiOfT5nK2+JopqUVJxce2Q4wDQYJKoZIhvcNAQEMBQADggIBAB/Kzt3H
vqGf2SdMC9wXmBFqiN495nFWcrKeGk6c1SuYJF2ba3uwM4IJvd8lRuqYnrYb/oM8
0mJhwQTtzuDFycgTE1XnqGOtjHsB/ncw4c5omwX4Eu55MaBBRTUoCnGkJE+M3DyC
B19m3H0Q/gxhswWV7uGugQ+o+MePTagjAiZrHYNSVc61LwDKgEDg4XSsYPWHgJ2u
NmSRXbBoGOqKYcl3qJfEycel/FVL8/B/uWU9J2jQzGv6U53hkRrJXRqWbTKH7QMg
yALOWr7Z6v2yTcQvG99fevX4i8buMTolUVVnjWQye+mew4K6Ki3pHrTgSAai/Gev
HyICc/sgCq+dVEuhzf9gR7A/Xe8bVr2XIZYtCtFenTgCR2y59PYjJbigapordwj6
xLEokCZYCDzifqrXPW+6MYgKBesntaFJ7qBFVHvmJ2WZICGoo7z7GJa7Um8M7YNR
TOlZ4iBgxcJlkoKM8xAfDoqXvneCbT+PHV28SSe9zE8P4c52hgQjxcCMElv924Sg
JPFI/2R80L5cFtHvma3AH/vLrrw4IgYmZNralw4/KBVEqE8AyvCazM90arQ+POuV
7LXTWtiBmelDGDfrs7vRWGJB82bSj6p4lVQgw1oudCvV0b4YacCs1aTPObpRhANl
6WLAYv7YTVWW4tAR+kg0Eeye7QUd5MjWHYbL
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPluILrIPglJ209ZjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjMwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjMwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AAQfTzOHMymKoYTey8chWEGJ6ladK0uFxh1MJ7x/JlFyb+Kf1qPKzEUURout736G
jOyxfi//qXGdGIRFBEFVbivqJn+7kAHjSxm65FSWRQmx1WyRRK2EE46ajA2ADDL2
4CejQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBTB8Sa6oC2uhYHP0/EqEr24Cmf9vDAKBggqhkjOPQQDAwNpADBmAjEA9uEglRR7
VKOQFhG/hMjqb2sXnh5GmCCbn9MN2azTL818+FsuVbu/3ZL3pAzcMeGiAjEA/Jdm
ZuVDFhOD3cffL74UOO0BzrEXGhF16b0DjyZ+hOXJYKaV11RZt+cRLInUue4X
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICCTCCAY6gAwIBAgINAgPlwGjvYxqccpBQUjAKBggqhkjOPQQDAzBHMQswCQYD
VQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2VzIExMQzEUMBIG
A1UEAxMLR1RTIFJvb3QgUjQwHhcNMTYwNjIyMDAwMDAwWhcNMzYwNjIyMDAwMDAw
WjBHMQswCQYDVQQGEwJVUzEiMCAGA1UEChMZR29vZ2xlIFRydXN0IFNlcnZpY2Vz
IExMQzEUMBIGA1UEAxMLR1RTIFJvb3QgUjQwdjAQBgcqhkjOPQIBBgUrgQQAIgNi
AATzdHOnaItgrkO4NcWBMHtLSZ37wWHO5t5GvWvVYRg1rkDdc/eJkTBa6zzuhXyi
QHY7qca4R9gq55KRanPpsXI5nymfopjTX15YhmUPoYRlBtHci8nHc8iMai/lxKvR
HYqjQjBAMA4GA1UdDwEB/wQEAwIBhjAPBgNVHRMBAf8EBTADAQH/MB0GA1UdDgQW
BBSATNbrdP9JNqPV2Py1PsVq8JQdjDAKBggqhkjOPQQDAwNpADBmAjEA6ED/g94D
9J+uHXqnLrmvT/aDHQ4thQEd0dlq7A/Cr8deVl5c1RxYIigL9zC2L7F8AjEA8GE8
p/SgguMh1YQdc4acLa/KNJvxn7kjNuK8YAOdgLOaVsjh4rsUecrNIdSUtUlD
-----END CERTIFICATE-----
)CERT";

String firebaseIdToken;
String firebaseRefreshToken;
// Persist only the refresh token in ESP32 NVS, separate from Wi-Fi preferences.
// NVS is persistent but not automatically encrypted on all ESP32 boards.
const char* FIREBASE_NVS_NAMESPACE = "fb-auth";
const char* FIREBASE_NVS_REFRESH_KEY = "refresh";
const char* FIREBASE_NVS_UID_KEY = "uid";
String firebaseTokenStorageState = "尚未讀取";
// A confirmed refresh-token rejection must not trigger repeated requests or erase
// the forensic copy in NVS. Persist the hold across reboot.
const char* FIREBASE_NVS_HOLD_KEY = "hold";
const char* FIREBASE_NVS_HASH_KEY = "hash";
bool firebaseRefreshErrorHold = false;
String firebaseRefreshDecision = "尚未換證";
unsigned long firebaseRefreshSuccessCount = 0;
unsigned long firebaseRefreshFailureCount = 0;
String firebaseLastRefreshError = "無";

// Non-cryptographic corruption check (NOT encryption or authentication).
uint32_t firebaseTokenChecksum(const String& value) {
  uint32_t h = 2166136261UL;
  for (size_t i = 0; i < value.length(); ++i) {
    h ^= (uint8_t)value[i];
    h *= 16777619UL;
  }
  return h;
}


void firebaseSaveRefreshToken() {
  if (firebaseRefreshToken.length() == 0) return;
  Preferences tokenPrefs;
  if (!tokenPrefs.begin(FIREBASE_NVS_NAMESPACE, false)) {
    firebaseTokenStorageState = "NVS 開啟失敗";
    return;
  }
  // Avoid needless flash writes when the token has not rotated.
  if (tokenPrefs.getString(FIREBASE_NVS_REFRESH_KEY, "") != firebaseRefreshToken ||
      tokenPrefs.getString(FIREBASE_NVS_UID_KEY, "") != FIREBASE_EXPECTED_UID) {
    size_t saved = tokenPrefs.putString(FIREBASE_NVS_REFRESH_KEY, firebaseRefreshToken);
    size_t uidSaved = tokenPrefs.putString(FIREBASE_NVS_UID_KEY, FIREBASE_EXPECTED_UID);
    size_t hashSaved = tokenPrefs.putUInt(FIREBASE_NVS_HASH_KEY, firebaseTokenChecksum(firebaseRefreshToken));
    firebaseTokenStorageState = (saved > 0 && uidSaved > 0 && hashSaved == sizeof(uint32_t)) ?
        "PASS：Refresh Token 已保存" : "NVS 寫入失敗";
  } else {
    firebaseTokenStorageState = "PASS：Refresh Token 已保存";
  }
  // A newly issued and validated token is allowed to clear a previous hold.
  if (firebaseRefreshErrorHold && firebaseTokenStorageState.startsWith("PASS")) {
    if (tokenPrefs.putBool(FIREBASE_NVS_HOLD_KEY, false) == sizeof(uint8_t))
      firebaseRefreshErrorHold = false;
  }
  tokenPrefs.end();
}

void firebaseClearStoredRefreshToken() {
  firebaseRefreshToken = "";
  Preferences tokenPrefs;
  if (tokenPrefs.begin(FIREBASE_NVS_NAMESPACE, false)) {
    tokenPrefs.remove(FIREBASE_NVS_REFRESH_KEY);
    tokenPrefs.remove(FIREBASE_NVS_UID_KEY);
    tokenPrefs.remove(FIREBASE_NVS_HASH_KEY);
    tokenPrefs.remove(FIREBASE_NVS_HOLD_KEY);
    tokenPrefs.end();
  }
  firebaseTokenStorageState = "已清除無效 Refresh Token";
}

void firebaseRestoreRefreshToken() {
  Preferences tokenPrefs;
  // Read-write creates the namespace on first boot; read-only fails if absent.
  if (!tokenPrefs.begin(FIREBASE_NVS_NAMESPACE, false)) {
    firebaseTokenStorageState = "NVS 開啟失敗";
    return;
  }
  String storedUid = tokenPrefs.getString(FIREBASE_NVS_UID_KEY, "");
  String storedToken = tokenPrefs.getString(FIREBASE_NVS_REFRESH_KEY, "");
  firebaseRefreshErrorHold = tokenPrefs.getBool(FIREBASE_NVS_HOLD_KEY, false);
  bool hasHash = tokenPrefs.isKey(FIREBASE_NVS_HASH_KEY);
  bool hashOk = !hasHash || tokenPrefs.getUInt(FIREBASE_NVS_HASH_KEY, 0) == firebaseTokenChecksum(storedToken);
  tokenPrefs.end();
  if (storedUid == FIREBASE_EXPECTED_UID && storedToken.length() > 20 && hashOk) {
    firebaseRefreshToken = storedToken;
    firebaseTokenStorageState = hasHash ? "PASS：已恢復且通過完整性檢查" :
        "PASS：已恢復舊版 Token（無校驗碼）";
    if (firebaseRefreshErrorHold) firebaseRefreshDecision = "先前憑證被拒：已持續暫停換證";
  } else {
    firebaseTokenStorageState = storedToken.length() && !hashOk ?
        "NVS Token 完整性檢查失敗：保留原資料，禁止自動登入" : "尚無可用的已保存 Token";
    if (storedToken.length() && !hashOk) {
      firebaseRefreshErrorHold = true;
      firebaseRefreshDecision = "儲存資料可能損壞：暫停驗證";
    }
  }
}

unsigned long firebaseRetryWaitMs = FIREBASE_RETRY_INTERVAL_MS;
String firebaseNextAuthMethod = "帳密登入";
unsigned long firebaseTokenLifetimeMs = 3300000UL;
String firebaseAuthenticatedUid = "尚未取得";
String firebaseUidCheck = "尚未檢查";
String firebaseTokenCheck = "尚未檢查";
String firebaseDbResponse = "尚未收到";
String firebaseAuthResponseError = "無";
bool firebaseCredentialErrorHold = false; // Avoid repeated password attempts when server rejects credentials.
unsigned long firebaseTokenAcquiredAt = 0;
unsigned long firebaseLastAttemptAt = 0;
unsigned long firebaseLastLoginAttemptAt = 0;
bool firebaseUploadAttempted = false;
bool firebaseLoginAttempted = false;
String firebaseAuthState = "尚未嘗試";
String firebaseUploadState = "尚未嘗試";
int firebaseAuthHttpCode = 0;
int firebaseUploadHttpCode = 0;
unsigned long firebaseLastAuthMs = 0;
unsigned long firebaseLastUploadMs = 0;
unsigned long firebaseLastSuccessMs = 0;

// Unified network diagnostics. No secrets or tokens are displayed.
String diagDns = "尚未檢查", diagTcp = "尚未檢查", diagTls = "尚未檢查";
String diagTlsUnverified = "尚未檢查（僅診斷，不用於 Firebase 傳輸）";
String diagAuthTlsDetail = "尚未檢查", diagUploadTlsDetail = "尚未檢查";
String diagAuthError = "無", diagUploadError = "無";
String diagProbeStage = "等待排程";
unsigned long diagLastProbeMs = 0;
unsigned long diagLastStageMs = 0;
uint8_t diagStage = 0;

String tlsErrorDetail(WiFiClientSecure& client) {
  char buffer[160] = {0};
  int code = client.lastError(buffer, sizeof(buffer));
  return String(code) + "：" + String(buffer);
}

IPAddress diagAuthIp;
bool diagDnsOk = false;

String diagHttpError(int code) {
  if (code > 0) {
    if (code == 400) return "HTTP 400：請求或帳號資料不符";
    if (code == 401) return "HTTP 401：身分驗證失敗或 Token 過期";
    if (code == 403) return "HTTP 403：權限不足或 API 設定限制";
    if (code == 404) return "HTTP 404：網址或資源不存在";
    if (code == 429) return "HTTP 429：請求過於頻繁";
    if (code >= 500) return "HTTP 5xx：伺服器端錯誤";
    return "HTTP " + String(code);
  }
  if (code == -1) return "-1：連線失敗（可能 DNS/TCP/TLS，需看分層檢查）";
  if (code == -2) return "-2：傳送標頭失敗";
  if (code == -3) return "-3：傳送資料失敗";
  if (code == -4) return "-4：連線中斷";
  if (code == -5) return "-5：連線遺失";
  if (code == -11) return "-11：讀取逾時";
  return "HTTPClient 代碼 " + String(code);
}

bool diagClockReady() { return time(nullptr) >= 1700000000; }

// One bounded network probe per loop pass; probes run every two minutes.
// TCP/TLS can still briefly block, so never run probes inside web handlers.
void updateNetworkDiagnostics() {
  if (setupMode || changePending || WiFi.status() != WL_CONNECTED) return;
  unsigned long now = millis();
  if (diagStage == 0) {
    if (diagLastProbeMs && now - diagLastProbeMs < DIAG_PROBE_INTERVAL_MS) return;
    diagLastProbeMs = now;
    diagStage = 1;
    diagProbeStage = "DNS";
    return;
  }
  if (diagStage == 1) {
    diagDnsOk = WiFi.hostByName("identitytoolkit.googleapis.com", diagAuthIp) == 1;
    diagDns = diagDnsOk ? "PASS：" + diagAuthIp.toString() : "FAIL：無法解析網域";
    diagStage = 2;
    diagProbeStage = "TCP 443";
    return;
  }
  if (diagStage == 2) {
    if (!diagDnsOk) { diagTcp = "SKIP：DNS 失敗"; diagStage = 3; return; }
    WiFiClient tcp;
    bool ok = tcp.connect(diagAuthIp, 443, 2000);
    diagTcp = ok ? "PASS：443 可連線" : "FAIL：443 連線失敗或逾時";
    tcp.stop();
    diagStage = 3;
    diagProbeStage = "TLS";
    return;
  }
  if (diagStage == 3) {
    if (!diagDnsOk || !diagClockReady() || !diagTcp.startsWith("PASS")) {
      diagTls = !diagClockReady() ? "SKIP：NTP 尚未同步" : "SKIP：DNS/TCP 未通過";
    } else {
      WiFiClientSecure tls;
      tls.setCACert(FIREBASE_ROOT_CAS);
      tls.setTimeout(3000);
      bool ok = tls.connect("identitytoolkit.googleapis.com", 443, 3000);
      diagTls = ok ? "PASS：憑證驗證成功" : "FAIL：" + tlsErrorDetail(tls);
      tls.stop();
    }
    // A diagnostic-only comparison: never use an unverified TLS session for
    // credentials, tokens, Firebase API calls, or any application data.
    diagStage = 4;
    diagProbeStage = "TLS 憑證差異檢查";
    return;
  }
  if (diagStage == 4) {
    if (!diagDnsOk || !diagClockReady() || !diagTcp.startsWith("PASS")) {
      diagTlsUnverified = "SKIP：前置網路檢查未通過";
    } else if (diagTls.startsWith("PASS")) {
      diagTlsUnverified = "不需要：正式憑證驗證已成功";
    } else {
      WiFiClientSecure probe;
      probe.setInsecure(); // DIAGNOSTIC ONLY; no credentials or HTTP request sent.
      probe.setTimeout(3000);
      bool ok = probe.connect("identitytoolkit.googleapis.com", 443, 3000);
      diagTlsUnverified = ok ? "PASS：無驗證握手可通；優先查憑證信任鏈" : "FAIL：無驗證握手也失敗；檢查 TLS 相容性/網路";
      if (!ok) diagTlsUnverified += "（" + tlsErrorDetail(probe) + "）";
      probe.stop();
    }
    diagStage = 0;
    diagProbeStage = "完成（定期重測）";
    diagLastStageMs = millis();
  }
}

// Extract a JSON string value from Firebase's response without another library.
// Tokens are base64url JWTs and contain no escaped characters.
String firebaseJsonString(const String& json, const char* key) {
  String needle = String("\"") + key + "\"";
  int pos = json.indexOf(needle);
  if (pos < 0) return "";
  pos = json.indexOf(':', pos + needle.length());
  if (pos < 0) return "";
  pos = json.indexOf('"', pos + 1);
  if (pos < 0) return "";
  int end = json.indexOf('"', pos + 1);
  if (end < 0) return "";
  return json.substring(pos + 1, end);
}

// Display only a server error field, never an ID token, password or full URL.
String firebaseSafeError(const String& body) {
  String msg = firebaseJsonString(body, "message");
  if (msg.length() == 0) msg = firebaseJsonString(body, "error");
  // RTDB sometimes returns a non-JSON error body.
  if (msg.length() == 0 && body.length() <= 160 &&
      body.indexOf("idToken") < 0 && body.indexOf("eyJ") < 0)
    msg = body;
  if (msg.length() > 150) msg = msg.substring(0, 150);
  String safe;
  for (unsigned int i = 0; i < msg.length(); ++i) {
    char c = msg[i];
    if (c >= 32 && c <= 126) safe += c;
  }
  return safe.length() ? safe : "未提供可顯示的錯誤內容";
}

bool firebaseLooksLikeJwt(const String& token) {
  int a = token.indexOf('.');
  int b = token.indexOf('.', a + 1);
  return token.length() > 100 && a > 0 && b > a + 1 && b < (int)token.length() - 1;
}

bool firebaseConfigured() {
  return String(FIREBASE_DEVICE_EMAIL).indexOf("ENTER_") != 0 &&
         String(FIREBASE_DEVICE_PASSWORD).indexOf("ENTER_") != 0 &&
         strlen(FIREBASE_DEVICE_EMAIL) > 3 && strlen(FIREBASE_DEVICE_PASSWORD) > 5;
}

bool firebaseSignIn() {
  firebaseLastAuthMs = millis();
  firebaseAuthState = "連線中";
  firebaseAuthHttpCode = 0;
  if (!diagClockReady()) {
    firebaseAuthState = "等待 NTP 時間同步";
    diagAuthError = "時間尚未同步，暫不嘗試 TLS";
    return false;
  }
  WiFiClientSecure client;
  client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  String url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FIREBASE_API_KEY;
  if (!http.begin(client, url)) { firebaseAuthState = "HTTPS 初始化失敗"; Serial.println("FIREBASE AUTH: HTTPS init failed"); return false; }
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  String payload = String("{\"email\":\"") + FIREBASE_DEVICE_EMAIL +
                   "\",\"password\":\"" + FIREBASE_DEVICE_PASSWORD +
                   "\",\"returnSecureToken\":true}";
  int status = http.POST(payload);
  diagAuthTlsDetail = status < 0 ? tlsErrorDetail(client) : "HTTPS 已收到 HTTP 回應";
  firebaseAuthHttpCode = status;
  String response = http.getString();
  http.end();
  if (status != 200) {
    firebaseAuthState = "登入失敗";
    firebaseAuthResponseError = firebaseSafeError(response);
    diagAuthError = diagHttpError(status) + " / " + firebaseAuthResponseError;
    Serial.printf("FIREBASE AUTH: FAIL HTTP %d (credentials not printed)\n", status);
    return false;
  }
  firebaseIdToken = firebaseJsonString(response, "idToken");
  String newRefreshToken = firebaseJsonString(response, "refreshToken");
  if (newRefreshToken.length()) firebaseRefreshToken = newRefreshToken;
  firebaseTokenLifetimeMs = 3300000UL;
  firebaseRetryWaitMs = FIREBASE_RETRY_INTERVAL_MS;
  firebaseAuthenticatedUid = firebaseJsonString(response, "localId");
  firebaseUidCheck = firebaseAuthenticatedUid == FIREBASE_EXPECTED_UID ?
      "PASS：登入 UID 與資料庫規則相符" : "FAIL：登入 UID 與資料庫規則不符";
  firebaseTokenCheck = firebaseLooksLikeJwt(firebaseIdToken) ?
      "PASS：ID Token 格式符合 JWT" : "FAIL：ID Token 格式不完整";
  if (!firebaseLooksLikeJwt(firebaseIdToken)) {
    firebaseAuthState = "回應缺少有效 idToken";
    Serial.println("FIREBASE AUTH: missing idToken");
    return false;
  }
  if (firebaseAuthenticatedUid.length() == 0 || firebaseAuthenticatedUid != FIREBASE_EXPECTED_UID) {
    firebaseAuthState = "UID 不符合預期";
    diagAuthError = "登入成功，但帳號 UID 不符合 RTDB 寫入規則";
    firebaseIdToken = "";
    return false;
  }
  firebaseAuthResponseError = "無";
  firebaseTokenAcquiredAt = millis();
  firebaseSaveRefreshToken();
  firebaseAuthState = "PASS";
  diagAuthError = "無";
  Serial.println("FIREBASE AUTH: PASS");
  return true;
}

// Exchange the refresh token for a new ID token without re-sending account credentials.
bool firebaseRefreshIdToken() {
  firebaseLastAuthMs = millis();
  firebaseAuthState = "更新 Token 中";
  firebaseRefreshDecision = "換證請求進行中";
  firebaseAuthHttpCode = 0;
  if (!diagClockReady()) {
    firebaseAuthState = "等待 NTP 時間同步";
    return false;
  }
  WiFiClientSecure client;
  client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  String url = String("https://securetoken.googleapis.com/v1/token?key=") + FIREBASE_API_KEY;
  if (!http.begin(client, url)) {
    firebaseAuthState = "Token 更新 HTTPS 初始化失敗";
    return false;
  }
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  String payload = String("grant_type=refresh_token&refresh_token=") + firebaseRefreshToken;
  int status = http.POST(payload);
  diagAuthTlsDetail = status < 0 ? tlsErrorDetail(client) : "HTTPS 已收到 HTTP 回應";
  firebaseAuthHttpCode = status;
  String response = http.getString();
  http.end();
  if (status != 200) {
    ++firebaseRefreshFailureCount;
    firebaseAuthState = "Token 更新失敗";
    firebaseAuthResponseError = firebaseSafeError(response);
    diagAuthError = diagHttpError(status) + " / " + firebaseAuthResponseError;
    firebaseLastRefreshError = firebaseAuthResponseError;
    bool credentialRejected = status == 400 && (
        firebaseAuthResponseError == "INVALID_REFRESH_TOKEN" ||
        firebaseAuthResponseError == "TOKEN_EXPIRED" ||
        firebaseAuthResponseError == "USER_DISABLED" ||
        firebaseAuthResponseError == "USER_NOT_FOUND");
    if (credentialRejected) {
      // Do not delete NVS evidence. Hold the rejected token across reboots.
      firebaseRefreshErrorHold = true;
      firebaseRefreshDecision = "Firebase 明確拒絕換證：已停止自動換證，NVS 原值保留";
      Preferences prefs;
      if (prefs.begin(FIREBASE_NVS_NAMESPACE, false)) {
        prefs.putBool(FIREBASE_NVS_HOLD_KEY, true);
        prefs.end();
      } else {
        firebaseTokenStorageState = "NVS 無法保存換證暫停狀態";
      }
    } else if (status == 400 || status == 401 || status == 403) {
      // Malformed requests, wrong project, and unknown client errors need inspection.
      // Repeating the identical request will not help.
      firebaseRefreshErrorHold = true;
      firebaseRefreshDecision = "換證請求遭拒（原因待查）：保留 Token 並暫停重試";
      Preferences prefs;
      if (prefs.begin(FIREBASE_NVS_NAMESPACE, false)) {
        prefs.putBool(FIREBASE_NVS_HOLD_KEY, true);
        prefs.end();
      }
    } else {
      firebaseRefreshDecision = "暫時性或網路失敗：保留 Token，延遲重試";
    }
    return false;
  }
  String newIdToken = firebaseJsonString(response, "id_token");
  String newUid = firebaseJsonString(response, "user_id");
  if (!firebaseLooksLikeJwt(newIdToken) || newUid != FIREBASE_EXPECTED_UID) {
    ++firebaseRefreshFailureCount;
    firebaseRefreshDecision = "回應驗證未通過：保留原 Token，暫停自動換證";
    firebaseRefreshErrorHold = true;
    firebaseAuthState = "Token 更新回應無效或 UID 不符";
    diagAuthError = firebaseAuthState;
    Preferences prefs;
    if (prefs.begin(FIREBASE_NVS_NAMESPACE, false)) {
      prefs.putBool(FIREBASE_NVS_HOLD_KEY, true);
      prefs.end();
    }
    return false;
  }
  String newRefreshToken = firebaseJsonString(response, "refresh_token");
  if (newRefreshToken.length()) firebaseRefreshToken = newRefreshToken;
  firebaseIdToken = newIdToken;
  firebaseAuthenticatedUid = newUid;
  firebaseUidCheck = "PASS：登入 UID 與資料庫規則相符";
  firebaseTokenCheck = "PASS：ID Token 格式符合 JWT";
  firebaseTokenAcquiredAt = millis();
  firebaseTokenLifetimeMs = 3300000UL;
  firebaseRetryWaitMs = FIREBASE_RETRY_INTERVAL_MS;
  firebaseSaveRefreshToken();
  ++firebaseRefreshSuccessCount;
  firebaseLastRefreshError = "無";
  firebaseRefreshDecision = "PASS：換證成功並保存最新 Refresh Token";
  firebaseAuthState = "PASS（Refresh Token）";
  firebaseAuthResponseError = "無";
  diagAuthError = "無";
  return true;
}

const bool D1_PAR_SCALE_VERIFIED = true; // SEN0641 FC03 Reg0 is PPFD in umol/m2/s; verified by lamp on/off test.
const float D1_PAR_SCALE = 1.0f; // ONLY change after checking SEN0641 documentation.
const unsigned long D1_SAMPLE_MS=10000UL, D1_LOG_MS=30000UL;
uint16_t d1ParRaw=0; bool d1ParRawValid=false, d1ParValid=false;
float d1Ppfd=NAN, d1Dli=0.0f;
unsigned long d1LastParAt=0, d1LastLogAt=0, d1LastGoodParAt=0;
bool d1SdMounted=false, d1SdLastWriteOK=false;
String d1SdError="NOT_STARTED";
// V4.25: separate full-rate PAR log; legacy 30-second environment CSV remains unchanged.
extern bool sdInitOK;
struct ParRecord425 { uint32_t epoch; uint32_t uptime; uint16_t raw; bool valid; };
ParRecord425 parQueue425[12];
uint8_t parCount425=0;
uint32_t parQueueOverflow425=0, parSdFailures425=0;
double parDliSum425=0.0;
uint32_t parDliSeconds425=0;
uint32_t parDliDay425=0;
uint32_t parLastEpoch425=0;
float parLastPpfd425=NAN;
bool parLastValid425=false;
// DLI is computed for intervals with adjacent valid readings and synchronized timestamps.
// The partial daily value is not marked authoritative until reboot reconstruction is implemented.
bool parDliProvisional425=true;
void parFlush425() {
  if (!parCount425 || !sdInitOK) return;
  uint8_t done=0;
  while (done<parCount425) {
    ParRecord425 &r=parQueue425[done];
    if(r.epoch<1700000000UL) { ++done; continue; }
    time_t tt=(time_t)r.epoch;
    struct tm tmv;
    if(!localtime_r(&tt,&tmv)) break;
    char day[16],stamp[36];
    strftime(day,sizeof(day),"%Y-%m-%d",&tmv);
    strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S%z",&tmv);
    if(!SD.exists("/logs")) SD.mkdir("/logs");
    String path=String("/logs/par_samples_")+day+".csv";
    bool fresh=!SD.exists(path);
    File f=SD.open(path,FILE_APPEND);
    if(!f) { ++parSdFailures425; break; }
    if(fresh) f.println("timestamp,epoch,uptime_ms,par_raw_reg0,par_raw_valid,ppfd_umol_m2_s,ppfd_valid,fw_version");
    String line=String(stamp)+","+String(r.epoch)+","+String(r.uptime)+","+
      (r.valid?String(r.raw):String(""))+","+(r.valid?"1":"0")+","+
      (r.valid?String(r.raw):String(""))+","+(r.valid?"1":"0")+","+FW_VERSION;
    size_t written=f.println(line); f.flush(); f.close();
    if(written<line.length()) { ++parSdFailures425; break; }
    ++done;
  }
  if(done) { for(uint8_t i=done;i<parCount425;i++) parQueue425[i-done]=parQueue425[i]; parCount425-=done; }
}
void parAdd425(uint16_t raw, bool valid, uint32_t uptime) {
  time_t now=time(nullptr);
  uint32_t epoch=(now>=1700000000)?(uint32_t)now:0;
  if(parCount425>=12) parFlush425();
  if(parCount425<12) parQueue425[parCount425++]={epoch,uptime,raw,valid};
  else ++parQueueOverflow425;
  if(!epoch) { parLastEpoch425=0; parLastValid425=false; return; }
  struct tm tmv;
  if(!localtime_r(&now,&tmv)) return;
  uint32_t day=uint32_t(tmv.tm_year+1900)*10000U+uint32_t(tmv.tm_mon+1)*100U+uint32_t(tmv.tm_mday);
  if(day!=parDliDay425) { parDliDay425=day; parDliSum425=0; parDliSeconds425=0; parLastEpoch425=0; parLastValid425=false; }
  if(valid && parLastValid425 && epoch>parLastEpoch425 && epoch-parLastEpoch425<=30) {
    uint32_t delta=epoch-parLastEpoch425;
    // Trapezoidal integration; do not fill long gaps with guessed light levels.
    parDliSum425+=(double(parLastPpfd425)+double(raw))*0.5*double(delta)/1000000.0;
    parDliSeconds425+=delta;
  }
  parLastEpoch425=epoch; parLastValid425=valid;
  parLastPpfd425=valid?float(raw):NAN;
  d1Dli=float(parDliSum425);
}


bool firebaseUpload() {
  firebaseLastUploadMs = millis();
  firebaseUploadHttpCode = 0;
  firebaseUploadState = "傳輸中";
  if (!shtOK || isnan(temperatureC) || isnan(humidityRH)) { firebaseUploadState = "感測器資料無效"; return false; }
  time_t now = time(nullptr);
  if (now < 1700000000) {
    firebaseUploadState = "等待 NTP 時間同步";
    Serial.println("FIREBASE: waiting for NTP time");
    return false;
  }
  WiFiClientSecure client;
  client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  String url = String("https://") + FIREBASE_DB_HOST +
      "/devices/esp32-s3/latest.json?auth=" + firebaseIdToken;
  if (!http.begin(client, url)) { firebaseUploadState = "HTTPS 初始化失敗"; Serial.println("FIREBASE PUT: HTTPS init failed"); return false; }
  http.setTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  String payload = String("{\"temperature_c\":") + String(temperatureC, 2) +
      ",\"humidity_rh\":" + String(humidityRH, 2) +
      ",\"timestamp\":" + String((unsigned long)now) +
      ",\"par_raw_valid\":" + String(d1ParRawValid?"true":"false") +
      ",\"par_raw_reg0\":" + String(d1ParRaw) +
      ",\"par_valid\":" + String(d1ParValid?"true":"false") +
      ",\"dli_valid\":false" +
      ",\"dli_provisional_mol_m2\":" + String(d1Dli,6) +
      ",\"sd_mounted\":" + String(d1SdMounted?"true":"false") +
      ",\"sd_last_write_ok\":" + String(d1SdLastWriteOK?"true":"false") +
      ",\"sd_error_code\":\"" + d1SdError + "\"" +
      ",\"fw_version\":\"" + FW_VERSION + "\"" +
      (d1ParValid ? String(",\"ppfd_umol_m2_s\":")+String(d1Ppfd,2) : String("")) + "}";
  int status = http.PUT(payload);
  diagUploadTlsDetail = status < 0 ? tlsErrorDetail(client) : "HTTPS 已收到 HTTP 回應";
  firebaseUploadHttpCode = status;
  // Capture the response before closing HTTPClient. A 401 response body often
  // explains whether this is invalid authentication or a database rule denial.
  String response = http.getString();
  firebaseDbResponse = status == 200 ? "PASS：資料庫接受 PUT" : firebaseSafeError(response);
  http.end();
  if (status == 200) {
    firebaseUploadState = "PASS";
    diagUploadError = "無";
    firebaseLastSuccessMs = millis();
    Serial.println("FIREBASE UPLOAD: PASS");
    return true;
  }
  firebaseUploadState = "上傳失敗";
  diagUploadError = diagHttpError(status) + " / " + firebaseDbResponse;
  Serial.printf("FIREBASE UPLOAD: FAIL HTTP %d\n", status);
  if (status == 401 || status == 403) firebaseIdToken = "";
  return false;
}

void updateFirebase() {
  if (setupMode || changePending || WiFi.status() != WL_CONNECTED) return;
  if (!firebaseConfigured()) {
    firebaseAuthState = "帳號或密碼未設定";
    return;
  }
  if (firebaseIdToken.length() && millis() - firebaseTokenAcquiredAt > firebaseTokenLifetimeMs)
    firebaseIdToken = "";
  if (!firebaseIdToken.length()) {
    if (firebaseRefreshErrorHold) return;
    if (firebaseCredentialErrorHold && firebaseRefreshToken.length() == 0) return;
    if (firebaseLoginAttempted && millis() - firebaseLastLoginAttemptAt < firebaseRetryWaitMs) return;
    firebaseLoginAttempted = true;
    firebaseLastLoginAttemptAt = millis();
    bool useRefresh = firebaseRefreshToken.length() > 0;
    firebaseNextAuthMethod = useRefresh ? "Refresh Token" : "帳密登入";
    bool ok = useRefresh ? firebaseRefreshIdToken() : firebaseSignIn();
    if (!ok) {
      if (!useRefresh && (firebaseAuthResponseError.indexOf("INVALID_LOGIN_CREDENTIALS") >= 0 ||
                          firebaseAuthResponseError.indexOf("INVALID_PASSWORD") >= 0 ||
                          firebaseAuthResponseError.indexOf("EMAIL_NOT_FOUND") >= 0)) {
        firebaseCredentialErrorHold = true;
        firebaseAuthState = "帳密遭拒：已暫停自動重試";
      }
      if (firebaseAuthResponseError.indexOf("TOO_MANY_ATTEMPTS_TRY_LATER") >= 0 ||
          firebaseAuthHttpCode == 429) {
        firebaseRetryWaitMs = FIREBASE_RATE_LIMIT_WAIT_MS;
      } else {
        unsigned long doubled = firebaseRetryWaitMs * 2UL;
        firebaseRetryWaitMs = firebaseRetryWaitMs >= FIREBASE_RATE_LIMIT_WAIT_MS ?
            FIREBASE_RATE_LIMIT_WAIT_MS : (doubled > FIREBASE_MAX_RETRY_INTERVAL_MS ?
            FIREBASE_MAX_RETRY_INTERVAL_MS : doubled);
      }
    }
    return;
  }
  if (firebaseUploadAttempted && millis() - firebaseLastAttemptAt < FIREBASE_UPLOAD_INTERVAL_MS) return;
  firebaseUploadAttempted = true;
  firebaseLastAttemptAt = millis();
  firebaseUpload();
}

String makePassword() {
  const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ"
                       "abcdefghijkmnopqrstuvwxyz23456789";
  String result;
  for (int i = 0; i < 12; i++) {
    result += chars[esp_random() % (sizeof(chars)-1)];
  }
  return result;
}

bool connectNetwork(const String& name, const String& pass) {
  if (name.length() == 0) return false;
  WiFi.mode(setupMode ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(name.c_str(), pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 18000) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

// V4.24 revised: keep OTA responsive while temporarily suspending blocking cloud calls.
// Local maintenance mode can be activated from a browser for 10 minutes.
// This does NOT erase data, change credentials, or alter the normal SD logger.
const unsigned long OTA_MAINTENANCE_WINDOW_MS = 10UL * 60UL * 1000UL;
bool otaMaintenanceActive = false;
bool otaTransferActive = false;
unsigned long otaMaintenanceStartedMs = 0;
uint32_t otaLongestLoopMs = 0;
uint32_t otaLastLoopDurationMs = 0;
uint32_t otaLongestCloudMs = 0;

bool otaCloudPaused() {
  if (otaTransferActive) return true;
  if (!otaMaintenanceActive) return false;
  if (millis() - otaMaintenanceStartedMs < OTA_MAINTENANCE_WINDOW_MS) return true;
  otaMaintenanceActive = false;
  return false;
}

void startOTA();

void otaService() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!otaReady) startOTA();
  if (otaReady) ArduinoOTA.handle();
}

void startOTA() {
  if (otaReady || WiFi.status() != WL_CONNECTED) return;
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(otaPassword.c_str());
  ArduinoOTA.onStart([]() {
    otaTransferActive = true;
    otaMaintenanceActive = true;
    otaMaintenanceStartedMs = millis();
    Serial.println("OTA: START (cloud uploads paused)");
  });
  ArduinoOTA.onEnd([]() { Serial.println("\nOTA: COMPLETE"); });
  ArduinoOTA.onError([](ota_error_t error) {
    otaTransferActive = false;
    otaMaintenanceActive = true;
    otaMaintenanceStartedMs = millis();
    Serial.printf("OTA ERROR: %u\n", (unsigned int)error);
  });
  ArduinoOTA.begin();
  otaReady = true;
  Serial.println("OTA: READY (port 3232)");
  Serial.print("OTA HOSTNAME: ");
  Serial.println(OTA_HOSTNAME);
}

void stopSetup() {
  setupMode = false;
  setupDNS.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  Serial.println("SETUP MODE: OFF");
}

void startSetup() {
  if (setupMode) return;
  setupMode = true;
  setupStarted = millis();
  provisionState = "idle";
  provisionIP = "";
  provisionSSID = "";
  provisionMessage = "";
  provisionError = "";
  provisionStage = "ap_ready";
  provisionSuccessAt = 0;

  WiFi.mode(WIFI_AP_STA);
  IPAddress apIP(192,168,4,1);
  if (!WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0))) {
    setupMode = false;
    Serial.println("SETUP MODE: AP_IP_CONFIG_FAIL");
    return;
  }
  if (WiFi.softAP(SETUP_SSID, setupPass)) {
    setupDNS.start(SETUP_DNS_PORT, "*", apIP);
    Serial.println("SETUP MODE: ON");
    Serial.print("SETUP SSID: "); Serial.println(SETUP_SSID);
    Serial.print("SETUP IP: "); Serial.println(PLANT_FACTORY_SETUP_IP);
    // Password is printed on the physical factory label, not over serial.
    Serial.println("SETUP PASSWORD: SEE DEVICE LABEL");
  } else {
    setupMode = false;
    provisionError = "E-01";
    provisionStage = "ap_start";
    Serial.println("SETUP MODE: FAIL");
  }
}


uint8_t shtCRC(const uint8_t* bytes, int count) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < count; ++i) {
    crc ^= bytes[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
  }
  return crc;
}

bool readSHT30(uint8_t addr) {
  Wire.beginTransmission(addr);
  Wire.write(0x24); Wire.write(0x00); // Single shot, high repeatability
  if (Wire.endTransmission() != 0) return false;
  delay(20);
  if (Wire.requestFrom(addr, (uint8_t)6) != 6) return false;
  uint8_t b[6];
  for (int i = 0; i < 6; ++i) b[i] = Wire.read();
  if (shtCRC(b, 2) != b[2] || shtCRC(b + 3, 2) != b[5]) return false;
  uint16_t tRaw = ((uint16_t)b[0] << 8) | b[1];
  uint16_t hRaw = ((uint16_t)b[3] << 8) | b[4];
  temperatureC = -45.0f + 175.0f * tRaw / 65535.0f;
  humidityRH = 100.0f * hRaw / 65535.0f;
  shtAddress = addr;
  return true;
}

void updateSHT30() {
  if (lastShtRead != 0 && millis() - lastShtRead < 2000) return;
  lastShtRead = millis();
  shtOK = readSHT30(shtAddress ? shtAddress : SHT_ADDRS[0]);
  if (!shtOK) {
    for (uint8_t addr : SHT_ADDRS) {
      if (addr != shtAddress && readSHT30(addr)) { shtOK = true; break; }
    }
  }
  if (shtOK) {
    Serial.printf("SHT30 PASS: %.2f C, %.2f %%RH (0x%02X)\n", temperatureC, humidityRH, shtAddress);
  } else {
    temperatureC = NAN; humidityRH = NAN; shtAddress = 0;
    Serial.println("SHT30 FAIL: check power, SDA=GPIO8, SCL=GPIO9");
  }
}


HardwareSerial parSerial(1);
const int PAR_EN=16, PAR_RX=17, PAR_TX=18;
uint16_t parCRC(const uint8_t* d,int n){uint16_t c=0xFFFF;for(int i=0;i<n;i++){c^=d[i];for(int j=0;j<8;j++)c=(c&1)?((c>>1)^0xA001):(c>>1);}return c;}
String parHex(uint8_t* d,int n){String s="";for(int i=0;i<n;i++){if(d[i]<16)s+="0";s+=String(d[i],HEX);s+=" ";}return s;}
String parProbe(uint8_t addr, uint8_t fn, uint16_t reg, bool highTx) {
  uint8_t q[8]={addr,fn,(uint8_t)(reg>>8),(uint8_t)reg,0,1,0,0};
  uint16_t c=parCRC(q,6);q[6]=c&255;q[7]=c>>8;
  const int txLevel=highTx?HIGH:LOW, rxLevel=highTx?LOW:HIGH;
  digitalWrite(PAR_EN,rxLevel);
  while(parSerial.available())parSerial.read();
  digitalWrite(PAR_EN,txLevel);delay(3);
  parSerial.write(q,8);parSerial.flush();delay(3);
  digitalWrite(PAR_EN,rxLevel);
  uint8_t r[32];int n=0;unsigned long t=millis();
  while(millis()-t<180 && n<32){
    if(parSerial.available())r[n++]=parSerial.read();else delay(1);
  }
  String x="Addr "+String(addr)+" FC"+String(fn)+" Reg"+String(reg)+": ";
  if(!n)return x+"無回應";
  x+="HEX "+parHex(r,n);
  if(n>=5){uint16_t z=r[n-2]|(r[n-1]<<8);x+=(parCRC(r,n-2)==z)?" CRC PASS":" CRC FAIL";}
  return x;
}


// D1 CANDIDATE: Reg0 has been observed responding to light, but its engineering
// units and scale are NOT yet verified. Do not publish an invented PPFD/DLI.


String sdTestMessage="NOT TESTED";
bool sdInitOK=false, sdWriteOK=false, sdReadOK=false;
bool d1ReadReg0(uint16_t &value) {
  uint8_t req[8]={1,3,0,0,0,1,0,0};
  uint16_t crc=parCRC(req,6);req[6]=crc&255;req[7]=crc>>8;
  digitalWrite(PAR_EN,LOW);
  while(parSerial.available())parSerial.read();
  digitalWrite(PAR_EN,HIGH);delay(3);
  parSerial.write(req,8);parSerial.flush();delay(3);
  digitalWrite(PAR_EN,LOW);
  uint8_t resp[7];int n=0;unsigned long began=millis();
  while(millis()-began<180UL&&n<7){if(parSerial.available())resp[n++]=parSerial.read();else delay(1);}
  if(n!=7||resp[0]!=1||resp[1]!=3||resp[2]!=2)return false;
  uint16_t received=(uint16_t)resp[5]|((uint16_t)resp[6]<<8);
  if(parCRC(resp,5)!=received)return false;
  value=((uint16_t)resp[3]<<8)|resp[4];return true;
}
void d1SamplePAR(){
  if(millis()-d1LastParAt<D1_SAMPLE_MS)return;
  d1LastParAt=millis();
  uint16_t raw=0;d1ParRawValid=d1ReadReg0(raw);
  d1ParValid=d1ParRawValid&&D1_PAR_SCALE_VERIFIED;
  if(d1ParRawValid){d1ParRaw=raw;d1LastGoodParAt=millis();}
  d1Ppfd=d1ParValid?(raw*D1_PAR_SCALE):NAN;
  parAdd425(raw,d1ParValid,millis());
}
void d1WriteLog(){
  if(millis()-d1LastLogAt<D1_LOG_MS)return;
  d1LastLogAt=millis();
  parFlush425();
  if(!sdInitOK){d1SdMounted=false;d1SdLastWriteOK=false;d1SdError="NOT_MOUNTED";return;}
  time_t now=time(nullptr);if(now<1700000000){d1SdError="TIME_UNSYNCED";return;}
  struct tm local; if(!localtime_r(&now,&local)){d1SdError="TIME_ERROR";return;}
  char day[20],stamp[36];strftime(day,sizeof(day),"%Y-%m-%d",&local);
  strftime(stamp,sizeof(stamp),"%Y-%m-%dT%H:%M:%S%z",&local);
  if(!SD.exists("/logs"))SD.mkdir("/logs");
  String path=String("/logs/")+day+".csv";
  bool isNew=!SD.exists(path);
  File file=SD.open(path,FILE_APPEND);
  if(!file){d1SdLastWriteOK=false;d1SdError="OPEN_FAIL";return;}
  if(isNew)file.println("timestamp,uptime_ms,temperature_c,rh_pct,par_raw_reg0,par_raw_valid,ppfd_umol_m2_s,ppfd_valid,dli_mol_m2,dli_valid,fw_version");
  String line=String(stamp)+","+String(millis())+","+(shtOK?String(temperatureC,2):"")+","+(shtOK?String(humidityRH,2):"")+","+(d1ParRawValid?String(d1ParRaw):"")+","+(d1ParRawValid?"1":"0")+","+(d1ParValid?String(d1Ppfd,2):"")+","+(d1ParValid?"1":"0")+","+String(d1Dli,6)+",0,"+FW_VERSION;
  size_t bytes=file.println(line);file.flush();file.close();
  d1SdMounted=true;d1SdLastWriteOK=bytes>=line.length();
  d1SdError=d1SdLastWriteOK?"NONE":"WRITE_FAIL";
}

// MicroSD integrated diagnostics; GPIO reservations unchanged.
const int SD_CS_PIN=10, SD_SCK_PIN=12, SD_MOSI_PIN=11, SD_MISO_PIN=13;
SPIClass sdSPI(FSPI);


void runMicroSDTest() {
  sdInitOK=sdWriteOK=sdReadOK=false;
  String report="<b>MicroSD integrated diagnostics V4.12</b>";
  report+="<p>SPI GPIO: CS=10 SCK=12 MOSI=11 MISO=13</p>";
  report+="<p>Power: 5V NOT measurable without external meter / ADC divider.</p>";
  report+="<p>Card filesystem type cannot be determined when mounting fails.</p>";
  sdSPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  const uint32_t rates[]={400000,1000000,4000000};
  int good=-1;
  for(int i=0;i<3;i++) {
    SD.end();
    digitalWrite(SD_CS_PIN,HIGH);
    delay(150);
    bool ok=SD.begin(SD_CS_PIN,sdSPI,rates[i]);
    report+="<p>SPI "+String(rates[i]/1000)+" kHz: "+String(ok?"MOUNT PASS":"MOUNT FAIL")+"</p>";
    if(ok){good=i;break;}
  }
  if(good<0) {
    report+="<p><b>SD INIT: FAIL</b></p>";
    report+="<p>Capacity: UNAVAILABLE; FAT32: UNDETERMINED; Read/write: NOT RUN</p>";
    report+="<p>Next: verify 5V with a multimeter, card insertion, or FAT32 on a computer. No voltage or format conclusion can be drawn from this failure.</p>";
    sdTestMessage=report;
    Serial.println("SD integrated test: INIT FAIL");
    return;
  }
  sdInitOK=true;
  report+="<p><b>SD INIT: PASS</b> at "+String(rates[good]/1000)+" kHz</p>";
  uint8_t ct=SD.cardType();
  String type=ct==CARD_NONE?"NONE":ct==CARD_MMC?"MMC":ct==CARD_SD?"SD":ct==CARD_SDHC?"SDHC/SDXC":"UNKNOWN";
  report+="<p>Card type: "+type+"</p>";
  report+="<p>Card capacity: "+String((double)SD.cardSize()/1073741824.0,2)+" GiB</p>";
  report+="<p>Filesystem mounted: PASS (FAT family); exact FAT16/FAT32 subtype NOT independently identified.</p>";
  const char* path="/sd_diagnostic.csv";
  const String marker="SD_DIAGNOSTIC_V4_12,GPIO10_12_11_13";
  File f=SD.open(path,FILE_WRITE);
  if(!f) {
    report+="<p>WRITE: FAIL (cannot open test file)</p>";
  } else {
    size_t n=f.println(marker);
    f.flush();f.close();
    sdWriteOK=(n==marker.length()+2 || n>=marker.length()+1);
    report+="<p>WRITE: "+String(sdWriteOK?"PASS":"FAIL")+" (bytes="+String(n)+")</p>";
  }
  f=SD.open(path,FILE_READ);
  if(!f) report+="<p>READ: FAIL (cannot open test file)</p>";
  else {
    bool found=false;
    while(f.available()) {String line=f.readStringUntil('\n');line.trim();if(line==marker)found=true;}
    f.close();sdReadOK=found;
    report+="<p>READ/VERIFY: "+String(found?"PASS":"FAIL")+"</p>";
  }
  report+="<p>Test file: /sd_diagnostic.csv (existing files not erased)</p>";
  sdTestMessage=report;
  Serial.println("SD integrated test: "+String(sdInitOK?"INIT PASS":"INIT FAIL")+", WRITE "+String(sdWriteOK?"PASS":"FAIL")+", READ "+String(sdReadOK?"PASS":"FAIL"));
}

// V4.15: isolated experimental key provisioning. NVS is NOT encrypted by this sketch.
// NEVER store production credentials until Flash Encryption / Secure Boot are configured.
static bool identity_provisioned = false;
static uint8_t identity_provisionedKey[32] = {0};
static void identity_loadProvisionedKey() {
  Preferences p;
  if (!p.begin("idv415", true)) return;
  if (p.getBytesLength("devkey") == sizeof(identity_provisionedKey)) {
    identity_provisioned = p.getBytes("devkey", identity_provisionedKey,
                                     sizeof(identity_provisionedKey)) == sizeof(identity_provisionedKey);
  }
  p.end();
}
static bool identity_writeProvisionedKey(const uint8_t key[32]) {
  Preferences p;
  if (!p.begin("idv415", false)) return false;
  bool ok = p.putBytes("devkey", key, 32) == 32;
  p.end();
  if (ok) { memcpy(identity_provisionedKey, key, 32); identity_provisioned = true; }
  return ok;
}
static bool identity_eraseProvisionedKey() {
  Preferences p;
  if (!p.begin("idv415", false)) return false;
  bool ok = p.remove("devkey");
  p.end();
  if (ok) { memset(identity_provisionedKey, 0, 32); identity_provisioned = false; }
  return ok;
}
static uint8_t identity_deviceKey[32];
static bool identity_keyLoaded = false;
static String identity_inputLine;

static int identity_hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
static bool identity_parseHex(const String &s, uint8_t *out, size_t maxLen, size_t &outLen) {
  if (s.length() == 0 || (s.length() & 1) || s.length() / 2 > maxLen) return false;
  outLen = s.length() / 2;
  for (size_t i = 0; i < outLen; i++) {
    int a = identity_hexDigit(s[2*i]), b = identity_hexDigit(s[2*i+1]);
    if (a < 0 || b < 0) return false;
    out[i] = (uint8_t)((a << 4) | b);
  }
  return true;
}
static String identity_toHex(const uint8_t *bytes, size_t len) {
  const char *digits = "0123456789abcdef";
  String s; s.reserve(len * 2);
  for (size_t i = 0; i < len; i++) {
    s += digits[bytes[i] >> 4];
    s += digits[bytes[i] & 0x0f];
  }
  return s;
}
static bool identity_computeHmac(const uint8_t *key, size_t keyLen,
                        const uint8_t *message, size_t messageLen,
                        uint8_t result[32]) {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  return mbedtls_md_hmac(info, key, keyLen, message, messageLen, result) == 0;
}
static void identity_printId() {
  uint8_t mac[6] = {0};
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    Serial.println("ERROR: chip identifier unavailable"); return;
  }
  Serial.print("CHIP_ID_MAC="); Serial.println(identity_toHex(mac, sizeof(mac)));
  Serial.println("NOTE: MAC is public identification, NOT authentication proof.");
}
static void identity_selfTest() {
  // RFC 4231 test case 1: key 0x0b x20; data 'Hi There'
  const uint8_t testKey[20] = {0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,
                                0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b};
  const uint8_t msg[] = {'H','i',' ','T','h','e','r','e'};
  const char *expected = "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7";
  uint8_t digest[32] = {0};
  bool ok = identity_computeHmac(testKey, sizeof(testKey), msg, sizeof(msg), digest);
  Serial.println(ok && identity_toHex(digest, sizeof(digest)) == expected ? "HMAC_SELF_TEST=PASS" : "HMAC_SELF_TEST=FAIL");
  memset(digest, 0, sizeof(digest));
}
static void identity_processCommand(String line) {
  line.trim();
  if (line == "STATUS") {
    Serial.println(identity_provisioned ? "PROVISIONED_TEST_NVS=YES" : "PROVISIONED_TEST_NVS=NO");
    Serial.println("WARNING: NVS key is not hardware-protected; TEST KEYS ONLY"); return;
  }
  if (line.startsWith("PROVISION ")) {
    // Physical confirmation is required. Provisioning cannot be triggered over HTTP.
    if (digitalRead(PLANT_BOOT_PIN) != LOW) { Serial.println("DENIED: hold BOOT while submitting PROVISION"); return; }
    if (identity_provisioned) { Serial.println("DENIED: already provisioned; erase explicitly first"); return; }
    uint8_t candidate[32] = {0}; size_t n = 0;
    bool valid = identity_parseHex(line.substring(10), candidate, sizeof(candidate), n) && n == 32;
    if (!valid) { memset(candidate, 0, 32); Serial.println("ERROR: require 64 hex characters"); return; }
    bool ok = identity_writeProvisionedKey(candidate);
    memset(candidate, 0, 32);
    Serial.println(ok ? "PROVISION_TEST_NVS=PASS" : "PROVISION_TEST_NVS=FAIL"); return;
  }
  if (line == "ERASEPROVISION") {
    if (digitalRead(PLANT_BOOT_PIN) != LOW) { Serial.println("DENIED: hold BOOT while submitting ERASEPROVISION"); return; }
    Serial.println(identity_eraseProvisionedKey() ? "ERASE_TEST_NVS=PASS" : "ERASE_TEST_NVS=FAIL"); return;
  }
  if (line.startsWith("AUTH ")) {
    if (!identity_provisioned) { Serial.println("ERROR: device key not provisioned"); return; }
    uint8_t nonce[64] = {0}; size_t n = 0;
    bool valid = identity_parseHex(line.substring(5), nonce, sizeof(nonce), n) && n >= 16;
    if (!valid) { Serial.println("ERROR: nonce must be 16..64 bytes of hex"); return; }
    uint8_t response[32] = {0};
    // Domain-separated message: prevents cross-protocol reuse of HMAC responses.
    uint8_t message[10+64] = {0};
    const char prefix[] = "plant-auth";
    memcpy(message, prefix, 10); memcpy(message + 10, nonce, n);
    bool ok = identity_computeHmac(identity_provisionedKey, 32, message, 10+n, response);
    Serial.println(ok ? "AUTH_RESPONSE=" + identity_toHex(response, 32) : "ERROR: HMAC failed");
    memset(response, 0, sizeof(response)); memset(message, 0, sizeof(message));
    memset(nonce, 0, sizeof(nonce)); return;
  }
  if (line == "ID") { identity_printId(); return; }
  if (line == "TEST") { identity_selfTest(); return; }
  if (line == "CLEAR") {
    memset(identity_deviceKey, 0, sizeof(identity_deviceKey)); identity_keyLoaded = false;
    Serial.println("TEST_KEY_CLEARED"); return;
  }
  if (line.startsWith("KEY ")) {
    uint8_t candidate[32] = {0}; size_t n = 0;
    bool ok = identity_parseHex(line.substring(4), candidate, sizeof(candidate), n) && n == sizeof(candidate);
    if (!ok) { memset(candidate, 0, sizeof(candidate)); Serial.println("ERROR: KEY must be exactly 64 hex characters"); return; }
    memcpy(identity_deviceKey, candidate, sizeof(identity_deviceKey));
    memset(candidate, 0, sizeof(candidate)); identity_keyLoaded = true;
    Serial.println("TEST_KEY_LOADED_IN_RAM_ONLY"); return;
  }
  if (line.startsWith("CHALLENGE ")) {
    if (!identity_keyLoaded) { Serial.println("ERROR: set test KEY first"); return; }
    uint8_t challenge[64] = {0}; size_t n = 0;
    if (!identity_parseHex(line.substring(10), challenge, sizeof(challenge), n)) {
      Serial.println("ERROR: challenge must be 2..128 hex characters, even length"); return;
    }
    uint8_t response[32] = {0};
    if (!identity_computeHmac(identity_deviceKey, sizeof(identity_deviceKey), challenge, n, response)) {
      Serial.println("ERROR: HMAC calculation failed"); return;
    }
    Serial.print("HMAC_RESPONSE="); Serial.println(identity_toHex(response, sizeof(response)));
    memset(response, 0, sizeof(response));
    memset(challenge, 0, sizeof(challenge));
    return;
  }
  if (line.length()) Serial.println("IDENTITY COMMANDS: ID | TEST | STATUS | PROVISION <64 hex> | AUTH <nonce hex> | ERASEPROVISION | KEY | CHALLENGE | CLEAR");
}

static void identityPollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      identity_processCommand(identity_inputLine);
      identity_inputLine = "";
    } else if (identity_inputLine.length() < 180) {
      identity_inputLine += c;
    } else {
      identity_inputLine = "";
      Serial.println("IDENTITY ERROR: command too long");
    }
  }
}

static bool identitySelfTestPassed() {
  const uint8_t k[20] = {0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b,0x0b};
  const uint8_t m[] = {'H','i',' ','T','h','e','r','e'};
  uint8_t out[32] = {0};
  bool ok = identity_computeHmac(k, sizeof(k), m, sizeof(m), out) &&
    identity_toHex(out, sizeof(out)) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7";
  memset(out, 0, sizeof(out));
  return ok;
}

// V4.17: Inspect NVS without creating namespaces, changing keys or erasing flash.
// A never-created namespace returns ESP_ERR_NVS_NOT_FOUND in read-only mode.
static String identity_keyNvsState() {
  nvs_handle_t handle = 0;
  esp_err_t err = nvs_open("idv415", NVS_READONLY, &handle);
  if (err == ESP_ERR_NVS_NOT_FOUND) return "NOT CONFIGURED: NAMESPACE ABSENT";
  if (err != ESP_OK) return "ERROR: NVS OPEN " + String(esp_err_to_name(err)) + " (" + String((int)err) + ")";

  size_t length = 0;
  err = nvs_get_blob(handle, "devkey", NULL, &length);
  if (err == ESP_ERR_NVS_NOT_FOUND) { nvs_close(handle); return "NOT CONFIGURED: KEY ABSENT"; }
  if (err != ESP_OK) { nvs_close(handle); return "ERROR: KEY METADATA " + String(esp_err_to_name(err)); }
  if (length != 32) { nvs_close(handle); return "ERROR: INVALID LENGTH (" + String((unsigned int)length) + ")"; }

  uint8_t temporary[32] = {0};
  size_t readLength = sizeof(temporary);
  err = nvs_get_blob(handle, "devkey", temporary, &readLength);
  memset(temporary, 0, sizeof(temporary));
  nvs_close(handle);
  if (err != ESP_OK) return "ERROR: KEY READ " + String(esp_err_to_name(err));
  return readLength == 32 ? "PASS: KEY PRESENT" : "ERROR: KEY READ LENGTH";
}

// V4.18 TEST ONLY. Local key generation on physical BOOT release (3-<5 s).
// The key is NEVER sent to the browser or serial. Existing Wi-Fi setup >=5 s remains unchanged.
static String identity_v418Event = "WAITING: no local provisioning attempt this boot";
static bool identity_v418ChallengePass = false;
static bool identity_v418VerifyPersistedKey() {
  // Verify using a fresh 32-byte nonce, and independently reload the key from NVS.
  uint8_t nonce[32] = {0}, expected[32] = {0}, actual[32] = {0};
  uint8_t stored[32] = {0};
  esp_fill_random(nonce, sizeof(nonce));
  const uint8_t prefix[] = {'p','l','a','n','t','-','a','u','t','h'};
  uint8_t message[sizeof(prefix) + sizeof(nonce)] = {0};
  memcpy(message, prefix, sizeof(prefix));
  memcpy(message + sizeof(prefix), nonce, sizeof(nonce));
  bool ok = identity_provisioned &&
    identity_computeHmac(identity_provisionedKey, 32, message, sizeof(message), expected);
  nvs_handle_t handle = 0;
  if (ok) {
    esp_err_t err = nvs_open("idv415", NVS_READONLY, &handle);
    if (err != ESP_OK) ok = false;
    else {
      size_t len = sizeof(stored);
      ok = nvs_get_blob(handle, "devkey", stored, &len) == ESP_OK && len == sizeof(stored);
      nvs_close(handle);
    }
  }
  if (ok) ok = identity_computeHmac(stored, sizeof(stored), message, sizeof(message), actual);
  if (ok) {
    uint8_t difference = 0;
    for (size_t i = 0; i < sizeof(actual); ++i) difference |= (actual[i] ^ expected[i]);
    ok = difference == 0;
  }
  memset(stored, 0, sizeof(stored)); memset(actual, 0, sizeof(actual));
  memset(expected, 0, sizeof(expected)); memset(message, 0, sizeof(message));
  memset(nonce, 0, sizeof(nonce));
  return ok;
}
static void identity_v418ProvisionLocally() {
  if (identity_provisioned || identity_keyNvsState() == "PASS: KEY PRESENT") {
    identity_v418Event = "DENIED: KEY ALREADY EXISTS (never overwritten)";
    return;
  }
  String current = identity_keyNvsState();
  if (!current.startsWith("NOT CONFIGURED:")) {
    identity_v418Event = "DENIED: NVS state abnormal; no write attempted";
    return;
  }
  uint8_t candidate[32] = {0};
  esp_fill_random(candidate, sizeof(candidate));
  bool ok = identity_writeProvisionedKey(candidate);
  memset(candidate, 0, sizeof(candidate));
  identity_v418ChallengePass = ok && identity_v418VerifyPersistedKey();
  identity_v418Event = !ok ? "FAIL: TEST KEY NVS WRITE" :
    identity_v418ChallengePass ? "PASS: TEST KEY STORED AND HMAC VERIFIED" :
    "FAIL: POST-WRITE HMAC VERIFICATION";
}

// V4.19 protocol staging: no cloud request until secure factory enrollment is complete.
// The V4.18 locally generated random key is unknown to the server. Never treat it as enrolled.
// This read-only page intentionally does not expose a challenge-signing oracle over HTTP.
static const char PAR_PHASE1_HTML[] PROGMEM = R"PARPAGE(<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>鹿角蕨環控｜每日歷史曲線</title><style>
:root{color-scheme:dark;--bg:#101f1a;--card:#21372c;--fg:#eef7f0;--muted:#b0c5ba}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font-family:-apple-system,Arial,sans-serif}main{max-width:520px;margin:auto;padding:16px}.card{background:var(--card);border-radius:14px;padding:14px;margin-top:12px}h1{font-size:22px;margin:4px 0}p,small{color:var(--muted)}.big{font-size:23px;font-weight:700;margin:8px 0}.row{display:flex;align-items:center;gap:8px;flex-wrap:wrap}input,button{font:inherit;border-radius:9px;padding:9px;background:#315b46;color:white;border:1px solid #739780}input{max-width:175px}svg{width:100%;height:auto;display:block}a{color:#a4dfb8}.meta{font-size:12px;line-height:1.7}.value{font-size:17px;font-weight:650}.metric{display:flex;justify-content:space-between;padding:5px 0;border-bottom:1px solid #405a4a}.hint{font-size:12px;color:#b0c5ba}
</style></head><body><main><small>PLATYCERIUM RESEARCH SYSTEM</small><h1>每日環境曲線</h1><p>V4.22 候選版 · 綠色手機介面</p>
<div class="card"><div class="metric"><span>溫度</span><span id="temp" class="value">--</span></div><div class="metric"><span>相對濕度</span><span id="rh" class="value">--</span></div><div class="metric"><span>PAR 原始值</span><span id="raw" class="value">--</span></div><div id="status" class="meta">連線中…</div></div>
<div class="card"><a href="/backend-test" style="display:block;text-align:center;background:#397e56;color:white;padding:13px;border-radius:12px;text-decoration:none;font-weight:700">後端測試結果 · PASS / FAIL / SKIP</a><p class="hint">開啟後可截圖傳送診斷結果；不顯示密碼或金鑰。</p></div>
<div class="card"><div class="row"><label for="date">紀錄日期</label><input type="date" id="date"><button id="load">讀取紀錄</button></div><p id="summary" class="hint">讀取 MicroSD 中…</p></div>
<div class="card"><b>溫度 °C</b><svg id="chartT" viewBox="0 0 350 220" role="img" aria-label="每日溫度曲線"></svg><p id="countT" class="hint"></p></div>
<div class="card"><b>相對濕度 %RH</b><svg id="chartH" viewBox="0 0 350 220" role="img" aria-label="每日濕度曲線"></svg><p id="countH" class="hint"></p></div>
<div class="card"><b>PAR 原始暫存器值</b><svg id="chartP" viewBox="0 0 350 220" role="img" aria-label="每日 PAR 原始值曲線"></svg><p id="countP" class="hint"></p><p class="hint">PAR 倍率及單位尚未驗證，暫不標示 PPFD／DLI。</p></div>
<p class="meta">資料來源：ESP32 MicroSD /logs/YYYY-MM-DD.csv。時間軸固定為當日 00:00–24:00；中斷或無效數值不補點。只有 ESP32 通電、時鐘校準及記憶卡正常的時段才有紀錄。重新開啟網頁會載入既有資料。</p><p><a href="/">ESP32 首頁</a> · <a href="/par">PAR 診斷</a></p></main><script>
const el=id=>document.getElementById(id);const pad=x=>String(x).padStart(2,'0');function today(){let n=new Date();return n.getFullYear()+'-'+pad(n.getMonth()+1)+'-'+pad(n.getDate())}el('date').value=today();
function plot(id,rows,unit,color){const svg=el(id);if(!rows.length){svg.innerHTML='<text x="175" y="105" text-anchor="middle" fill="#b0c5ba" font-size="12">當日無有效紀錄</text>';return}let min=Math.min(...rows.map(p=>p.y)),max=Math.max(...rows.map(p=>p.y));if(min===max){min-=1;max+=1}let L=44,R=12,T=12,B=30,W=350,H=220,PW=W-L-R,PH=H-T-B;const X=x=>L+x/86400*PW,Y=y=>T+(max-y)/(max-min)*PH;let g='';for(let i=0;i<=4;i++){let y=T+i*PH/4;g+=`<line x1="${L}" y1="${y}" x2="${W-R}" y2="${y}" stroke="#617c6b" opacity=".5"/><text x="${L-4}" y="${y+3}" fill="#b0c5ba" text-anchor="end" font-size="9">${(max-(max-min)*i/4).toFixed(1)}</text>`}for(let h=0;h<=24;h+=6){g+=`<text x="${X(h*3600)}" y="${H-7}" text-anchor="middle" font-size="10" fill="#b0c5ba">${pad(h)}:00</text>`}let seg=[],last=null;function flush(){if(seg.length>1)g+=`<polyline points="${seg.join(' ')}" fill="none" stroke="${color}" stroke-width="2"/>`;else if(seg.length===1){let [x,y]=seg[0].split(',');g+=`<circle cx="${x}" cy="${y}" r="2.5" fill="${color}"/>`}seg=[]}for(let p of rows){if(last!==null&&(p.x-last>90||p.x<=last))flush();seg.push(`${X(p.x).toFixed(2)},${Y(p.y).toFixed(2)}`);last=p.x}flush();svg.innerHTML=g}
async function latest(){try{let [a,b]=await Promise.all([fetch('/data',{cache:'no-store'}),fetch('/api/par/latest',{cache:'no-store'})]);let j=await b.json();el('raw').textContent=j.raw_valid?j.raw_reg0:'讀取失敗';el('status').textContent='MicroSD：'+(j.sd_mounted?'已掛載':'未掛載')+' · 寫入：'+(j.sd_write_ok?'正常':'待確認');if(a.ok){let x=await a.json();let t=x.temperature_c??x.temperature??x.temp,r=x.humidity_rh??x.humidity??x.rh;el('temp').textContent=Number.isFinite(Number(t))&&t!==null?Number(t).toFixed(2)+' °C':'--';el('rh').textContent=Number.isFinite(Number(r))&&r!==null?Number(r).toFixed(2)+' %RH':'--'}}catch(e){el('status').textContent='即時資料錯誤：'+e.message}}
async function load(){let day=el('date').value;el('summary').textContent='載入 '+day+' 紀錄中…';try{let r=await fetch('/api/par/history?date='+encodeURIComponent(day),{cache:'no-store'});if(!r.ok)throw Error(r.status===404?'該日期尚無紀錄':'HTTP '+r.status+' '+await r.text());let csv=await r.text(),lines=csv.trim().split(/\r?\n/),headers=lines.shift().split(','),idx=n=>headers.indexOf(n);for(let n of ['timestamp','temperature_c','rh_pct','par_raw_reg0','par_raw_valid'])if(idx(n)<0)throw Error('缺少 CSV 欄位：'+n);let t=[],h=[],p=[],total=0;for(let line of lines){if(!line.trim())continue;let c=line.split(','),m=(c[idx('timestamp')]||'').match(/T(\d\d):(\d\d):(\d\d)/);if(!m)continue;let x=+m[1]*3600+(+m[2])*60+(+m[3]);if(x>=86400)continue;total++;let tv=c[idx('temperature_c')],hv=c[idx('rh_pct')],pv=c[idx('par_raw_reg0')];if(tv!==''&&Number.isFinite(+tv)&&+tv>=-40&&+tv<=125)t.push({x,y:+tv});if(hv!==''&&Number.isFinite(+hv)&&+hv>=0&&+hv<=100)h.push({x,y:+hv});if(c[idx('par_raw_valid')]==='1'&&pv!==''&&Number.isFinite(+pv))p.push({x,y:+pv})}for(let a of [t,h,p])a.sort((a,b)=>a.x-b.x);plot('chartT',t,'°C','#8cdea4');plot('chartH',h,'%RH','#7ac7e6');plot('chartP',p,'raw','#f3c477');el('countT').textContent='有效 '+t.length+' 筆';el('countH').textContent='有效 '+h.length+' 筆';el('countP').textContent='有效 '+p.length+' 筆';el('summary').textContent=day+' · 紀錄 '+total+' 筆（每 30 秒一次，依實際運作情況）'}catch(e){for(let id of ['chartT','chartH','chartP'])plot(id,[]);el('summary').textContent='歷史紀錄：'+e.message;for(let id of ['countT','countH','countP'])el(id).textContent=''}}
el('load').addEventListener('click',load);latest();load();setInterval(latest,10000);setInterval(()=>{if(el('date').value===today())load()},60000);
</script></body></html>)PARPAGE";

// DS3231 stores UTC; ESP32 timezone controls local display (Asia/Taipei UTC+8).
// No extra RTC library is needed. Only NTP callback can authorize RTC writes.
volatile bool rtcNtpSyncPending = false;
bool rtcFound = false, rtcReadValid = false, rtcBootRestore = false;
bool rtcNtpWritten = false;
unsigned long rtcLastNtpWriteMs = 0;
String rtcLastError = "NOT_TESTED";
void rtcNtpCallback(struct timeval* tv) { rtcNtpSyncPending = true; }
uint8_t rtcBcd(int n) { return uint8_t((n / 10) * 16 + (n % 10)); }
int rtcDec(uint8_t n) { return (n >> 4) * 10 + (n & 15); }
bool rtcReadUtc(time_t& result) {
  Wire.beginTransmission(0x68);
  if (Wire.endTransmission() != 0) { rtcFound=false; rtcLastError="I2C_NO_ACK"; return false; }
  rtcFound=true;
  Wire.beginTransmission(0x68); Wire.write((uint8_t)0);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((uint8_t)0x68,(uint8_t)7) != 7) {
    rtcLastError="REGISTER_READ_FAIL"; return false;
  }
  uint8_t b[7]; for(int i=0;i<7;i++) b[i]=Wire.read();
  if (b[2]&0x40) { rtcLastError="UNSUPPORTED_12H_MODE"; return false; }
  struct tm utc={};
  utc.tm_sec=rtcDec(b[0]&0x7f); utc.tm_min=rtcDec(b[1]&0x7f);
  utc.tm_hour=rtcDec(b[2]&0x3f); utc.tm_mday=rtcDec(b[4]&0x3f);
  utc.tm_mon=rtcDec(b[5]&0x1f)-1; utc.tm_year=rtcDec(b[6])+100;
  if(utc.tm_sec>59||utc.tm_min>59||utc.tm_hour>23||utc.tm_mday<1||utc.tm_mday>31||utc.tm_mon<0||utc.tm_mon>11||utc.tm_year<124||utc.tm_year>199) {
    rtcLastError="INVALID_OR_UNSET_TIME";return false;
  }
  // Convert UTC calendar fields to Unix time without timegm() (ESP32 Core 2.0.3).
  const int year=utc.tm_year+1900, month=utc.tm_mon+1, day=utc.tm_mday;
  static const int monthDays[]={31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap=(year%4==0 && (year%100!=0 || year%400==0));
  if(day>monthDays[month-1]+((month==2 && leap)?1:0)) {
    rtcLastError="INVALID_CALENDAR_DATE";return false;
  }
  const int y=year-(month<=2 ? 1 : 0);
  const int era=y/400;
  const unsigned yoe=(unsigned)(y-era*400);
  const unsigned mp=(unsigned)(month+(month>2?-3:9));
  const unsigned doy=(153*mp+2)/5+(unsigned)day-1;
  const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
  const int64_t days=(int64_t)era*146097+(int64_t)doe-719468;
  const int64_t seconds=days*86400LL+utc.tm_hour*3600LL+utc.tm_min*60LL+utc.tm_sec;
  if(seconds<1700000000LL || seconds>4102444799LL) {
    rtcLastError="INVALID_EPOCH";return false;
  }
  result=(time_t)seconds;
  rtcReadValid=true;rtcLastError="PASS";return true;
}
bool rtcWriteUtc(time_t epoch) {
  if(epoch<1700000000) {rtcLastError="NTP_TIME_INVALID";return false;}
  struct tm t={}; if(!gmtime_r(&epoch,&t)) return false;
  if(t.tm_year<100||t.tm_year>199) {rtcLastError="YEAR_OUT_OF_RANGE";return false;}
  Wire.beginTransmission(0x68); Wire.write((uint8_t)0);
  Wire.write(rtcBcd(t.tm_sec)); Wire.write(rtcBcd(t.tm_min));
  Wire.write(rtcBcd(t.tm_hour)); Wire.write(rtcBcd(t.tm_wday+1));
  Wire.write(rtcBcd(t.tm_mday)); Wire.write(rtcBcd(t.tm_mon+1));
  Wire.write(rtcBcd(t.tm_year-100));
  uint8_t e=Wire.endTransmission();
  if(e) {rtcLastError="WRITE_ACK_"+String(e);return false;}
  rtcNtpWritten=true;rtcLastNtpWriteMs=millis();rtcLastError="PASS";return true;
}
void rtcRestoreAtBoot() {
  time_t t=0;
  if(rtcReadUtc(t)) {
    struct timeval tv={};tv.tv_sec=t;tv.tv_usec=0;
    rtcBootRestore=(settimeofday(&tv,nullptr)==0);
    Serial.println(rtcBootRestore?"RTC BOOT RESTORE: PASS":"RTC BOOT RESTORE: FAIL");
  } else Serial.println("RTC BOOT RESTORE: "+rtcLastError);
}
void rtcServiceNtp() {
  if(!rtcNtpSyncPending)return;
  rtcNtpSyncPending=false;
  time_t now=time(nullptr);
  if(now>=1700000000) {
    bool ok=rtcWriteUtc(now);
    Serial.println(ok?"RTC NTP WRITE: PASS":"RTC NTP WRITE: FAIL "+rtcLastError);
  }
}
String rtcClockString(time_t t, bool local) {
  struct tm v={}; if(local)localtime_r(&t,&v);else gmtime_r(&t,&v);
  char b[32];strftime(b,sizeof(b),"%Y-%m-%d %H:%M:%S",&v);return String(b);
}

void setupRoutes() {
  web.on("/rtc", HTTP_GET, []() {
    if(setupMode){web.send(503,"text/plain","Setup mode");return;}
    time_t chip=0;bool ok=rtcReadUtc(chip);
    time_t now=time(nullptr);
    String h="<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head><body style='font-family:sans-serif;padding:18px'>";
    h+="<h2>V4.30 DS3231 / NTP</h2>";
    h+="<p>Firmware: "+String(FW_VERSION)+"</p>";
    h+="<p>DS3231 0x68: "+String(rtcFound?"PASS":"FAIL")+"</p>";
    h+="<p>RTC read: "+String(ok?"PASS":"FAIL")+" ("+rtcLastError+")</p>";
    h+="<p>RTC UTC: "+String(ok?rtcClockString(chip,false):"INVALID")+"</p>";
    h+="<p>RTC Taiwan: "+String(ok?rtcClockString(chip+28800,false):"INVALID")+"</p>";
    h+="<p>System Taiwan: "+String(now>=1700000000?rtcClockString(now,true):"NOT SET")+"</p>";
    h+="<p>Boot restored from RTC: "+String(rtcBootRestore?"YES":"NO")+"</p>";
    h+="<p>NTP-to-RTC synchronized this boot: "+String(rtcNtpWritten?"PASS":"WAITING")+"</p>";
    h+="<p>SHT30: "+String(shtOK?"PASS":"FAIL")+"</p>";
    h+="<p>RTC stores UTC; displayed Taiwan time is UTC+8. RTC time may be wrong until NTP sync succeeds.</p>";
    h+="<p><a href='/'>Back to home</a></p></body></html>";
    web.sendHeader("Cache-Control","no-store");web.send(200,"text/html; charset=utf-8",h);
  });

  web.on("/cloudauth", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    uint8_t mac[6] = {0};
    bool idOK = esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;
    String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='Cache-Control' content='no-store'><title>V4.19 Cloud Auth</title></head>"
      "<body style='font-family:Arial,sans-serif;padding:20px'><h2>V4.19 雲端挑戰回應協定</h2>";
    html += "<p>韌體：" + String(FW_VERSION) + "</p>";
    html += "<p>設備識別碼：" + (idOK ? identity_toHex(mac, 6) : String("ERROR")) + "</p>";
    html += "<p>本機測試密鑰：" + String(identity_keyNvsState() == "PASS: KEY PRESENT" ? "已配置" : "未配置或異常") + "</p>";
    html += "<p>雲端設備註冊：NOT ENROLLED / NOT VERIFIED</p>";
    html += "<p>雲端挑戰取得：NOT ATTEMPTED</p>";
    html += "<p>雲端 HMAC 驗證：NOT ATTEMPTED</p>";
    html += "<p>Firebase Custom Token：NOT ISSUED</p>";
    html += "<p>安全鎖定：V4.18 本機隨機密鑰未與伺服器共享，禁止自動冒充正式設備登入。</p>";
    html += "<p>下一階段須完成安全工廠配置、伺服器部署及端對端測試，才可啟用雲端驗證。</p>";
    html += "<p><a href='/keystatus'>密鑰狀態</a> | <a href='/'>首頁</a></p></body></html>";
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    web.send(200, "text/html; charset=utf-8", html);
  });
  web.on("/keystatus", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    uint8_t mac[6] = {0};
    bool idOK = esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;
    String state = identity_keyNvsState();
    bool present = state == "PASS: KEY PRESENT";
    String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='Cache-Control' content='no-store'><title>V4.18 NVS Status</title></head>"
      "<body style='font-family:Arial,sans-serif;padding:20px'><h2>V4.19 設備密鑰診斷</h2>";
    html += "<p>韌體：" + String(FW_VERSION) + "</p>";
    html += "<p>晶片識別碼 (MAC，非密碼)：" + (idOK ? identity_toHex(mac, 6) : String("ERROR")) + "</p>";
    html += "<p>設備測試密鑰：" + String(present ? "已配置" : state.startsWith("NOT CONFIGURED") ? "未配置" : "狀態異常") + "</p>";
    html += "<p>NVS 讀取狀態：" + state + "</p>";
    html += "<p>本次開機配置結果：" + identity_v418Event + "</p>";
    html += "<p>隨機挑戰／NVS 重讀驗證：" + String(present ? (identity_v418VerifyPersistedKey() ? "PASS" : "FAIL") : "NOT RUN (KEY ABSENT)") + "</p>";
    html += "<p>測試配置方式：短按 BOOT 約 3–4 秒後放開；按住 5 秒以上仍進入原有 Wi-Fi 設定。</p>";
    html += "<p>HMAC-SHA256 RFC4231 自我測試：" + String(identitySelfTestPassed() ? "PASS" : "FAIL") + "</p>";
    html += "<p>注意：NVS 未加密，僅限測試密鑰；此頁不顯示密鑰，也不提供寫入或刪除功能。</p>";
    html += "<p>此頁位於區域網路 HTTP，請勿公開至網際網路。</p>";
    html += "<p><a href='/identity'>身分診斷</a> | <a href='/'>返回首頁</a></p></body></html>";
    web.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    web.send(200, "text/html; charset=utf-8", html);
  });

  // V4.15 identity diagnostics: no secret or HMAC response exposed over HTTP.
  web.on("/identity", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    uint8_t mac[6] = {0};
    bool idOK = esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK;
    String html = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'></head>"
      "<body style='font-family:Arial,sans-serif;padding:20px'><h2>V4.19 設備身分診斷</h2>";
    html += "<p>韌體：" + String(FW_VERSION) + "</p>";
    html += "<p>晶片識別碼 (MAC，非密碼)：" + (idOK ? identity_toHex(mac, 6) : String("ERROR")) + "</p>";
    html += "<p>HMAC-SHA256 RFC4231 自我測試：" + String(identitySelfTestPassed() ? "PASS" : "FAIL") + "</p>";
    html += "<p>本版本僅供測試；密鑰存於未加密 NVS，不會向 Firebase 申請設備身分。</p>";
    html += "<p><a href='/keystatus'>設備密鑰狀態</a> | <a href='/cloudauth'>雲端驗證狀態</a> | <a href='/'>返回首頁</a></p></body></html>";
    web.send(200, "text/html; charset=utf-8", html);
  });

  web.on("/", HTTP_GET, []() {
    if (!setupMode) {
      String html = "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='15'>"
        "<style>body{font-family:-apple-system,Arial,sans-serif;max-width:560px;"
        "margin:35px auto;padding:18px;background:#f6faf7;color:#20382c}"
        ".card{background:white;border-radius:16px;padding:22px;box-shadow:0 2px 12px #ddd}"
        "a{color:#16764b}</style></head><body><div class='card'>"
        "<h2>鹿角蕨環控系統 · " + String(FW_VERSION) + "</h2>"
        "<p>連線狀態：" + String(WiFi.status() == WL_CONNECTED ? "已連線" : "未連線") + "</p>"
        "<p>Wi-Fi 名稱：" + wifiName + "</p>"
        "<p>裝置 IP：" + WiFi.localIP().toString() + "</p>"
        "<p>訊號強度：" + String(WiFi.RSSI()) + " dBm</p>"
        "<p>運作時間：" + String(millis()/1000UL) + " 秒</p>"
        "<p>OTA 更新：" + String(otaReady ? "已啟用" : "未啟用") + "</p>"
        "<h3>SHT30 溫濕度感測器</h3>"
        "<p>感測器狀態：" + String(shtOK ? "PASS（讀取成功）" : "FAIL（未偵測到）") + "</p>"
        "<p>溫度：" + String(shtOK ? String(temperatureC, 2) + " °C" : "--") + "</p>"
        "<p>相對濕度：" + String(shtOK ? String(humidityRH, 2) + " %RH" : "--") + "</p>"
        "<p>I2C 位址：" + String(shtOK ? (shtAddress == 0x44 ? "0x44" : "0x45") : "--") + "</p>"
        "<p><a href='/data'>查看 JSON 資料</a></p>"
        "<p><a href='/firebase'>Firebase 登入與上傳診斷</a></p>"
        "<p><a href='/par'>PAR RS485 唯讀診斷</a> | <a href='/par-history'>溫濕度與 PAR 每日曲線</a> | <a href='/backend-test'>後端測試結果</a></p>"
        "<p><a href='/i2c'>執行 I2C 位址掃描</a></p>"
        "<p><a href='/shtdiag'>執行 SHT30 深度診斷</a></p><p><a href='/shtstatus'>SHT30 狀態與重置診斷</a></p>"
        "<p style='font-size:13px'>需要更換 Wi-Fi：長按開發板 BOOT 約 5 秒，"
        "再連線 " + String(SETUP_SSID) + " 並開啟 192.168.4.1。</p>"
        "<p><b>韌體版本：" + String(FW_VERSION) + "</b></p>"
        "<p>本次頁面更新時間：<span id='now'>讀取中</span></p>"
        "<p>更新紀錄（最近 10 次）：</p><ol id='history'></ol>"
        "<p style='font-size:12px'>每 15 秒自動重新載入；時間取自 iPhone／電腦瀏覽器</p>"
        "<script>"
        "var now=new Date().toLocaleString('zh-TW',{hour12:false});"
        "document.getElementById('now').textContent=now;"
        "var key='plant-v32-refresh-log';"
        "var arr=[];try{arr=JSON.parse(sessionStorage.getItem(key)||'[]');}catch(e){}"
        "arr.unshift(now);arr=arr.slice(0,10);"
        "try{sessionStorage.setItem(key,JSON.stringify(arr));}catch(e){}"
        "document.getElementById('history').innerHTML=arr.map(function(t){"
        "return '<li>'+t+'</li>';}).join('');"
        "</script>"
        "</div></body></html>";
      web.send(200, "text/html; charset=utf-8", html);
    } else {
      web.sendHeader("Location", "/setup");
      web.send(302, "text/plain", "");
    }
  });

  // V4.20 phase 1: read-only local PAR history from the existing SD CSV.
  // No network credentials or tokens are included. LAN access is not authenticated;
  // use only on trusted networks, not exposed to the public Internet.
  web.on("/api/par/history", HTTP_GET, []() {
    if (setupMode) { web.send(503,"text/plain","Setup mode"); return; }
    if (!sdInitOK) { web.send(503,"text/plain","SD_NOT_MOUNTED"); return; }
    String day=web.arg("date");
    if(day.length()!=10 || day[4]!='-' || day[7]!='-') { web.send(400,"text/plain","DATE_REQUIRED_YYYY-MM-DD"); return; }
    for(int i=0;i<10;i++) if(i!=4 && i!=7 && (day[i]<'0'||day[i]>'9')) {
      web.send(400,"text/plain","INVALID_DATE"); return;
    }
    String path=String("/logs/")+day+".csv";
    File f=SD.open(path,FILE_READ);
    if(!f) { web.send(404,"text/plain","NO_LOG_FOR_DATE"); return; }
    // Existing firmware writes at 30 s intervals, normally under 200 KB/day.
    // Refuse unexpectedly large files to bound RAM and response size.
    if(f.size()>300000) { f.close();web.send(413,"text/plain","LOG_TOO_LARGE");return; }
    web.sendHeader("Cache-Control","no-store");
    web.streamFile(f,"text/csv; charset=utf-8");
    f.close();
  });
  web.on("/api/par/latest", HTTP_GET, []() {
    if (setupMode) { web.send(503,"text/plain","Setup mode"); return; }
    String j="{\"raw_valid\":"+String(d1ParRawValid?"true":"false")+
      ",\"raw_reg0\":"+String(d1ParRaw)+
      ",\"ppfd_valid\":"+String(d1ParValid?"true":"false")+
      ",\"ppfd\":"+(d1ParValid?String(d1Ppfd,2):String("null"))+
      ",\"dli_provisional\":"+String(d1Dli,6)+
      ",\"dli_valid\":false"+
      ",\"par_buffered\":"+String(parCount425)+
      ",\"par_buffer_overflow\":"+String(parQueueOverflow425)+
      ",\"par_sd_failures\":"+String(parSdFailures425)+
      ",\"sd_mounted\":"+String(d1SdMounted?"true":"false")+
      ",\"sd_write_ok\":"+String(d1SdLastWriteOK?"true":"false")+
      ",\"last_good_age_ms\":"+(d1LastGoodParAt?String(millis()-d1LastGoodParAt):String("null"))+"}";
    web.sendHeader("Cache-Control","no-store");web.send(200,"application/json",j);
  });
  // V4.22: read-only snapshot. Never include Wi-Fi passwords, Firebase tokens or HMAC material.
  web.on("/backend-test", HTTP_GET, []() {
    if (setupMode) { web.send(503,"text/plain","Setup mode"); return; }
    time_t now=time(nullptr);
    bool clockOK=now>=1700000000;
    String date="";
    if(clockOK){struct tm t;if(localtime_r(&now,&t)){char buf[16];strftime(buf,sizeof(buf),"%Y-%m-%d",&t);date=buf;}}
    String path=String("/logs/")+date+".csv";
    bool logExists=sdInitOK && date.length() && SD.exists(path);
    size_t logBytes=0;
    if(logExists){File f=SD.open(path,FILE_READ);if(f){logBytes=f.size();f.close();}}
    bool parRecent=d1LastGoodParAt && (millis()-d1LastGoodParAt<60000UL);
    bool logRecent=d1LastLogAt && (millis()-d1LastLogAt<90000UL);
    String page=F("<!doctype html><html lang='zh-Hant'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='30'><style>body{font-family:-apple-system,Arial,sans-serif;background:#102019;color:#eff8f0;margin:0;padding:18px}main{max-width:560px;margin:auto}.card{background:#243b2f;border-radius:15px;padding:14px;margin:10px 0;overflow-wrap:anywhere}.pass{color:#93e5a6}.fail{color:#ffac9f}.skip{color:#ffdd8a}a{color:#9fdfb0}code{color:#fff}</style></head><body><main><h2>後端測試結果</h2><p>每 30 秒自動更新 · 唯讀診斷</p>");
    auto row=[&](const String &name,const String &state,const String &detail){
      String cls=state=="PASS"?"pass":(state=="FAIL"?"fail":"skip");
      page+="<div class='card'><b>"+name+"</b><p class='"+cls+"'>"+state+"</p><small>"+detail+"</small></div>";
    };
    row("韌體","PASS",FW_VERSION);
    row("Wi-Fi",WiFi.status()==WL_CONNECTED?"PASS":"FAIL",WiFi.status()==WL_CONNECTED?"已連線":"未連線");
    row("時鐘 / NTP",clockOK?"PASS":"FAIL",clockOK?"已取得有效日期":"時間未同步，無法按日期寫入 CSV");
    row("SHT30 溫濕度",shtOK?"PASS":"FAIL",shtOK?("溫度 "+String(temperatureC,2)+" °C；濕度 "+String(humidityRH,2)+" %RH"):"目前讀取失敗，檢查 I2C、接線、供電及探頭");
    row("PAR RS-485",parRecent?"PASS":"FAIL",parRecent?("原始暫存器 0："+String(d1ParRaw)+"；最近有效資料距今 "+String((millis()-d1LastGoodParAt)/1000UL)+" 秒"):"最近 60 秒沒有有效 Modbus 回應；檢查 A/B、供電、位址及 CRC");
    row("PAR 換算 PPFD",d1ParValid?"PASS":"SKIP",d1ParValid?"已啟用經確認的倍率":"PPFD 已按 SEN0641 原廠 Reg0 定義啟用；DLI 仍屬未完成恢復驗證的暫估值");
    row("MicroSD 掛載",sdInitOK?"PASS":"FAIL",sdInitOK?"SD 初始化成功":"初始化失敗，檢查 SPI、CS、供電及記憶卡");
    row("CSV 定時寫入",!sdInitOK||!clockOK?"SKIP":(d1SdLastWriteOK&&logRecent?"PASS":"FAIL"),"錯誤代碼："+d1SdError+"；最近寫入程序距今 "+(d1LastLogAt?String((millis()-d1LastLogAt)/1000UL)+" 秒":"尚未執行"));
    row("今日 CSV 檔案",!clockOK||!sdInitOK?"SKIP":(logExists?"PASS":"FAIL"),date.length()?(path+"；大小 "+String((unsigned long)logBytes)+" bytes"):"等待時鐘同步");
    row("每日歷史曲線 API",!clockOK||!sdInitOK?"SKIP":(logExists?"PASS":"FAIL"),logExists?"可從 /api/par/history?date=YYYY-MM-DD 讀取":"尚無當日紀錄或記憶卡不可讀");
    page+="<div class='card'><b>狀態說明</b><p>PASS：本機檢查通過；FAIL：檢查失敗或目前沒有有效資料；SKIP：前置條件不足或功能尚未啟用。</p><p>此頁不會主動執行破壞性測試、清除資料或重新初始化硬體。</p><p><a href='/par-history'>返回每日環境曲線</a> · <a href='/sdtest'>MicroSD 診斷</a> · <a href='/par'>PAR 診斷</a></p></div></main></body></html>";
    web.sendHeader("Cache-Control","no-store");web.send(200,"text/html; charset=utf-8",page);
  });

  web.on("/environment-history", HTTP_GET, []() {
    if (setupMode) { web.send(503,"text/plain","Setup mode"); return; }
    web.send_P(200,"text/html; charset=utf-8",PAR_PHASE1_HTML);
  });
  web.on("/par-history", HTTP_GET, []() {
    if (setupMode) { web.send(503,"text/plain","Setup mode"); return; }
    // Serve from flash in chunks; no SD reads are required for the HTML itself.
    web.send_P(200,"text/html; charset=utf-8",PAR_PHASE1_HTML);
  });

  web.on("/data", HTTP_GET, []() {
    if (setupMode) {
      web.send(503, "text/plain", "Setup mode");
      return;
    }
    String json = "{\"device\":\"ESP32-S3\",\"firmware\":\"" + String(FW_VERSION) + "\",";
    json += "\"wifi_connected\":";
    json += WiFi.status() == WL_CONNECTED ? "true" : "false";
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"uptime_ms\":" + String(millis());
    json += ",\"rssi_dbm\":" + String(WiFi.RSSI());
    json += ",\"sht30_ok\":" + String(shtOK ? "true" : "false");
    json += ",\"temperature_c\":" + String(shtOK ? String(temperatureC, 2) : "null");
    json += ",\"humidity_rh\":" + String(shtOK ? String(humidityRH, 2) : "null");
    json += "}";
    web.send(200, "application/json", json);
  });

  // Read-only local diagnostic page. No API keys, credentials or ID tokens exposed.
  web.on("/firebase", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    String html = "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='15'>"
      "<style>body{font-family:-apple-system,Arial,sans-serif;max-width:560px;margin:25px auto;padding:16px}"
      "p{line-height:1.6}code{word-break:break-word}</style></head><body>"
      "<h2>Firebase 診斷 · " + String(FW_VERSION) + "</h2>"
      "<p>Wi-Fi：" + String(WiFi.status() == WL_CONNECTED ? "PASS" : "FAIL") + "</p>"
      "<p>IP：" + WiFi.localIP().toString() + " / 閘道：" + WiFi.gatewayIP().toString() + "</p>"
      "<p>RSSI：" + String(WiFi.RSSI()) + " dBm</p>"
      "<p>DNS：" + diagDns + "</p>"
      "<p>NTP：" + String(diagClockReady() ? "PASS" : "FAIL / 等待同步") + "</p>"
      "<p>TCP：" + diagTcp + "</p>"
      "<p>TLS（憑證驗證）：" + diagTls + "</p>"
      "<p>TLS（不驗證憑證，僅握手測試）：" + diagTlsUnverified + "</p>"
      "<p>檢查階段：" + diagProbeStage + "；最後完成開機秒數：" + String(diagLastStageMs / 1000UL) + "</p>"
      "<p>SHT30：" + String(shtOK ? "PASS" : "FAIL") + "</p>"
      "<p>Firebase 帳密已設定：" + String(firebaseConfigured() ? "是" : "否") + "</p>"
      "<p>登入狀態：" + firebaseAuthState + "</p>"
      "<p>登入 HTTP 代碼：" + String(firebaseAuthHttpCode) + "</p>"
      "<p>登入錯誤：" + diagAuthError + "</p>"
      "<p>登入 TLS 詳細資訊：" + diagAuthTlsDetail + "</p>"
      "<p>登入嘗試時的開機秒數：" + String(firebaseLastAuthMs / 1000UL) + "</p>"
      "<p>Token 存在：" + String(firebaseIdToken.length() ? "是" : "否") + "</p>"
      "<p>Refresh Token 存在：" + String(firebaseRefreshToken.length() ? "是" : "否") + "</p>"
      "<p>Token NVS 儲存狀態：" + firebaseTokenStorageState + "</p>"
      "<p>Refresh Token 安全暫停：" + String(firebaseRefreshErrorHold ? "是" : "否") + "</p>"
      "<p>Refresh Token 換證判斷：" + firebaseRefreshDecision + "</p>"
      "<p>Refresh Token 換證成功次數：" + String(firebaseRefreshSuccessCount) + "</p>"
      "<p>Refresh Token 換證失敗次數：" + String(firebaseRefreshFailureCount) + "</p>"
      "<p>最近換證伺服器錯誤：" + firebaseLastRefreshError + "</p>"
      "<p>帳密錯誤自動重試保護：" + String(firebaseCredentialErrorHold ? "已暫停（需先核對帳號登入方式）" : "未觸發") + "</p>"
      "<p>最近驗證方式：" + firebaseNextAuthMethod + "</p>"
      "<p>登入重試間隔：" + String(firebaseRetryWaitMs / 1000UL) + " 秒</p>"
      "<p>下次允許登入距今：" + String(firebaseCredentialErrorHold || firebaseIdToken.length() || !firebaseLoginAttempted ? 0UL :
          (millis() - firebaseLastLoginAttemptAt >= firebaseRetryWaitMs ? 0UL :
          (firebaseRetryWaitMs - (millis() - firebaseLastLoginAttemptAt)) / 1000UL)) + " 秒</p>"
      "<p>Token 格式檢查：" + firebaseTokenCheck + "</p>"
      "<p>Firebase 登入 UID：" + firebaseAuthenticatedUid + "</p>"
      "<p>UID 規則比對：" + firebaseUidCheck + "</p>"
      "<p>登入伺服器錯誤：" + firebaseAuthResponseError + "</p>"
      "<p>上傳狀態：" + firebaseUploadState + "</p>"
      "<p>上傳 HTTP 代碼：" + String(firebaseUploadHttpCode) + "</p>"
      "<p>上傳錯誤：" + diagUploadError + "</p>"
      "<p>資料庫回應：" + firebaseDbResponse + "</p>"
      "<p>上傳 TLS 詳細資訊：" + diagUploadTlsDetail + "</p>"
      "<p>上傳嘗試時的開機秒數：" + String(firebaseLastUploadMs / 1000UL) + "</p>"
      "<p>最後成功上傳的開機秒數：" + String(firebaseLastSuccessMs / 1000UL) + "</p>"
      "<p>溫度：" + String(shtOK ? String(temperatureC, 2) : "--") + " °C</p>"
      "<p>濕度：" + String(shtOK ? String(humidityRH, 2) : "--") + " %RH</p>"
      "<p>備註：無驗證 TLS 僅進行空白握手測試，不傳送帳密或資料；正式 Firebase 連線仍強制驗證憑證。</p>"
      "<p>HTTP 負數代表本機傳輸錯誤；401/403 可能與授權或規則有關。</p>"
      "<p>每 15 秒自動更新。<a href='/'>返回首頁</a></p></body></html>";
    web.send(200, "text/html; charset=utf-8", html);
  });

  web.on("/diagnostics", HTTP_GET, []() {
    web.sendHeader("Location", "/firebase");
    web.send(302, "text/plain", "Redirecting to /firebase");
  });

  web.on("/sdtest", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    String page = "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "</head><body style='font-family:Arial,sans-serif;padding:20px'>"
      "<h2>MicroSD Comprehensive Test - V4.12</h2>"
      "<p>" + sdTestMessage + "</p>"
      "<p><a href='/sdtest/run'>Run test again</a></p>"
      "<p><a href='/'>Home</a></p></body></html>";
    web.send(200, "text/html; charset=utf-8", page);
  });
  web.on("/sdtest/run", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    runMicroSDTest();
    web.sendHeader("Location", "/sdtest");
    web.send(302, "text/plain", "Redirecting");
  });

  // On-demand scan; only runs when /i2c is opened.
  web.on("/i2c", HTTP_GET, []() {
    if (setupMode) {
      web.send(503, "text/plain", "Setup mode");
      return;
    }
    String result = "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "</head><body style='font-family:Arial,sans-serif;padding:20px'>"
      "<h2>I2C Scan - V3.3</h2><p>SDA: GPIO8 / SCL: GPIO9 / 100 kHz</p>";
    int count = 0;
    bool found44 = false, found45 = false;
    for (uint8_t address = 1; address < 127; ++address) {
      Wire.beginTransmission(address);
      uint8_t error = Wire.endTransmission();
      if (error == 0) {
        result += "<p>Device found: 0x";
        if (address < 16) result += "0";
        result += String(address, HEX);
        result += "</p>";
        ++count;
        if (address == 0x44) found44 = true;
        if (address == 0x45) found45 = true;
      }
    }
    result += "<p>Total devices: " + String(count) + "</p>";
    result += "<p>0x44: " + String(found44 ? "FOUND" : "NOT FOUND") + "</p>";
    result += "<p>0x45: " + String(found45 ? "FOUND" : "NOT FOUND") + "</p>";
    result += "<p>SHT30 read: " + String(shtOK ? "PASS" : "FAIL") + "</p>";
    result += "<p><a href='/'>Back to dashboard</a></p></body></html>";
    web.send(200, "text/html; charset=utf-8", result);
  });

  // On-demand SHT30 transaction diagnostics. No credentials are displayed.
  web.on("/shtdiag", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    const uint8_t addr = 0x44;
    String result = "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
      "<body style='font-family:Arial,sans-serif;padding:20px'>"
      "<h2>SHT30 Deep Diagnosis - V3.5</h2>"
      "<p>SDA GPIO8 / SCL GPIO9 / Address 0x44</p>";
    Wire.beginTransmission(addr);
    uint8_t ack = Wire.endTransmission();
    result += "<p>Address ACK code: " + String(ack) + " (0 = OK)</p>";
    Wire.beginTransmission(addr);
    Wire.write(0x24);
    Wire.write(0x00);
    uint8_t cmd = Wire.endTransmission();
    result += "<p>Measurement command code: " + String(cmd) + " (0 = OK)</p>";
    if (cmd == 0) {
      delay(25);
      uint8_t n = Wire.requestFrom(addr, (uint8_t)6);
      result += "<p>Received bytes: " + String(n) + " / 6</p>";
      uint8_t b[6] = {0};
      for (uint8_t i = 0; i < n && i < 6; ++i) b[i] = Wire.read();
      if (n == 6) {
        result += "<p>Raw HEX: ";
        for (uint8_t i = 0; i < 6; ++i) {
          if (b[i] < 16) result += "0";
          result += String(b[i], HEX);
          result += (i == 5 ? "" : " ");
        }
        result += "</p>";
        uint8_t tc = shtCRC(b, 2), hc = shtCRC(b + 3, 2);
        result += "<p>Temperature CRC: " + String(tc == b[2] ? "PASS" : "FAIL") + "</p>";
        result += "<p>Humidity CRC: " + String(hc == b[5] ? "PASS" : "FAIL") + "</p>";
        if (tc == b[2] && hc == b[5]) {
          uint16_t tr = ((uint16_t)b[0] << 8) | b[1];
          uint16_t hr = ((uint16_t)b[3] << 8) | b[4];
          result += "<p>Temperature: " + String(-45.0f + 175.0f * tr / 65535.0f, 2) + " C</p>";
          result += "<p>Humidity: " + String(100.0f * hr / 65535.0f, 2) + " %RH</p>";
        }
      }
    }
    result += "<p><a href='/'>Back to dashboard</a></p></body></html>";
    web.send(200, "text/html; charset=utf-8", result);
  });

  // Diagnostic endpoint: runs only when requested, keeps existing Wi-Fi and OTA intact.
  web.on("/shtstatus", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }
    const uint8_t addr = 0x44;
    String out = "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
      "<body style='font-family:Arial,sans-serif;padding:20px'>"
      "<h2>SHT30 Status Diagnosis - V3.5</h2>"
      "<p>SDA GPIO8 / SCL GPIO9 / Address 0x44</p>";
    Wire.beginTransmission(addr);
    out += "<p>Address ACK: " + String(Wire.endTransmission()) + "</p>";
    // SHT3x soft reset: 0x30A2
    Wire.beginTransmission(addr);
    Wire.write(0x30); Wire.write(0xA2);
    uint8_t resetCode = Wire.endTransmission();
    out += "<p>Soft reset command: " + String(resetCode) + " (0 = ACK)</p>";
    delay(30);
    // Read SHT3x status register: 0xF32D; expect 3 bytes including CRC.
    Wire.beginTransmission(addr);
    Wire.write(0xF3); Wire.write(0x2D);
    uint8_t statusCode = Wire.endTransmission();
    out += "<p>Status command: " + String(statusCode) + " (0 = ACK)</p>";
    if (statusCode == 0) {
      delay(5);
      uint8_t count = Wire.requestFrom(addr, (uint8_t)3);
      out += "<p>Status bytes: " + String(count) + " / 3</p>";
      uint8_t b[3] = {0};
      for (uint8_t i=0; i<count && i<3; ++i) b[i] = Wire.read();
      if (count == 3) {
        out += "<p>Status raw: 0x" + String(((uint16_t)b[0]<<8)|b[1], HEX) + "</p>";
        out += "<p>Status CRC: " + String(shtCRC(b,2)==b[2] ? "PASS" : "FAIL") + "</p>";
      }
    }
    // Single shot, no clock stretching, low repeatability 0x2416
    Wire.beginTransmission(addr);
    Wire.write(0x24); Wire.write(0x16);
    uint8_t lowCode = Wire.endTransmission();
    out += "<p>Low-repeatability command: " + String(lowCode) + " (0 = ACK)</p>";
    if (lowCode == 0) {
      delay(100);
      uint8_t count = Wire.requestFrom(addr, (uint8_t)6);
      out += "<p>Measurement bytes after 100 ms: " + String(count) + " / 6</p>";
      uint8_t b[6] = {0};
      for (uint8_t i=0; i<count && i<6; ++i) b[i] = Wire.read();
      if (count == 6) {
        out += "<p>Temperature CRC: " + String(shtCRC(b,2)==b[2] ? "PASS" : "FAIL") + "</p>";
        out += "<p>Humidity CRC: " + String(shtCRC(b+3,2)==b[5] ? "PASS" : "FAIL") + "</p>";
        if (shtCRC(b,2)==b[2] && shtCRC(b+3,2)==b[5]) {
          uint16_t t=((uint16_t)b[0]<<8)|b[1], h=((uint16_t)b[3]<<8)|b[4];
          out += "<p>Temperature: " + String(-45.0f+175.0f*t/65535.0f,2) + " C</p>";
          out += "<p>Humidity: " + String(100.0f*h/65535.0f,2) + " %RH</p>";
        }
      }
    }
    out += "<p><a href='/'>Back to dashboard</a></p></body></html>";
    web.send(200, "text/html; charset=utf-8", out);
  });


  // PAR V4.2: scan only read-only registers with previously verified settings.
  web.on("/par", HTTP_GET, []() {
    if (setupMode) { web.send(503, "text/plain", "Setup mode"); return; }

    const bool highTx = true; // GPIO16 HIGH = transmit, LOW = receive
    parSerial.end();
    digitalWrite(PAR_EN, LOW);
    parSerial.begin(4800, SERIAL_8N1, PAR_RX, PAR_TX);
    delay(30);

    String x = "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>PAR RS485 診斷 V4.2</title></head>"
      "<body style='font-family:Arial,sans-serif;max-width:850px;margin:auto;padding:16px'>"
      "<h2>PAR RS485 多暫存器診斷 · V4.2</h2>"
      "<p>4800 baud / 8N1 / 站號 1 / GPIO16 EN（HIGH TX、LOW RX）"
      " / GPIO17 RX / GPIO18 TX</p>"
      "<p>唯讀 FC03 與 FC04，分別讀取暫存器 0–19，每次讀取 1 個暫存器。</p>"
      "<p><b>請先遮光掃描一次，再用檯燈照射掃描一次，比較兩份結果。</b></p>"
      "<p>CRC PASS 只代表回應傳輸正確，不代表該暫存器一定是 PPFD。</p>"
      "<p><a href='/par'>重新掃描</a> | <a href='/'>返回首頁</a></p>"
      "<table border='1' cellpadding='6' cellspacing='0' "
      "style='border-collapse:collapse;font-size:13px;max-width:100%;word-break:break-word'>"
      "<tr><th>功能碼</th><th>暫存器</th><th>結果 / 原始 HEX</th></tr>";

    int valid = 0, responses = 0;
    for (uint8_t fn = 3; fn <= 4; ++fn) {
      for (uint16_t reg = 0; reg < 20; ++reg) {
        String result = parProbe(1, fn, reg, highTx);
        if (result.indexOf("CRC PASS") >= 0) ++valid;
        if (result.indexOf("無回應") < 0) ++responses;
        x += "<tr><td>FC" + String(fn) + "</td><td>" + String(reg)
          + "</td><td>" + result + "</td></tr>";
        if (otaReady && WiFi.status() == WL_CONNECTED) ArduinoOTA.handle();
        yield();
      }
    }
    // Leave the bus in receive mode at the verified baud rate.
    digitalWrite(PAR_EN, LOW);
    x += "</table><p>收到回應次數：" + String(responses)
       + "；CRC PASS 次數：" + String(valid) + "</p>"
       "<p>若有 Modbus 異常回應或全部數值不變，仍須核對感測器的暫存器表與供電規格。</p>"
       "<p><a href='/par'>再次掃描</a> | <a href='/'>返回首頁</a></p>"
       "</body></html>";
    web.send(200, "text/html; charset=utf-8", x);
  });

  // V4.29 provisioning UI: no router-IP lookup or manual copy required.
  web.on("/setup", HTTP_GET, []() {
    if (!setupMode) { web.send(403, "text/plain", "Setup disabled"); return; }
    touchSetup();
    String html =
      "<!doctype html><html lang='zh-Hant'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:-apple-system,Arial,sans-serif;max-width:520px;margin:auto;padding:20px;line-height:1.6}input,button{font-size:17px;padding:12px;max-width:100%;box-sizing:border-box}input{width:100%}button{margin-top:14px}</style>"
      "</head><body><h2>鹿角蕨 Wi-Fi 設定</h2>"
      "<form action='/save' method='POST'>"
      "<p>選擇您使用的家用 Wi-Fi</p><select id='ssidSelect' style='width:100%;font-size:17px;padding:12px' onchange=\"document.getElementById('ssidInput').value=(this.value==='__manual__'?'':this.value);document.getElementById('ssidInput').style.display=(this.value==='__manual__'?'block':'none')\"><option value='__manual__'>手動輸入（隱藏網路或掃描不到）</option></select>"
      "<input id='ssidInput' name='ssid' maxlength='32' placeholder='家用 Wi-Fi 名稱' required>"
      "<p>家用 Wi-Fi 密碼</p><input name='password' type='password' maxlength='63'>"
      "<p><button type='submit'>儲存並測試連線</button></p></form>"
      "<p>設定熱點基本時間 10 分鐘；操作期間自動延長。</p>"
      "<script>fetch('/api/wifi-scan',{cache:'no-store'}).then(r=>r.json()).then(j=>{let sel=document.getElementById('ssidSelect'),inp=document.getElementById('ssidInput');(j.networks||[]).forEach(n=>{let o=document.createElement('option');o.value=n;o.textContent=n;sel.insertBefore(o,sel.lastElementChild)});if(sel.options.length>1){sel.selectedIndex=0;inp.value=sel.value;inp.style.display='none'}}).catch(()=>{});</script>"
      "</body></html>";
    web.send(200, "text/html; charset=utf-8", html);
  });

  // Wi-Fi discovery for the phone UI. Never return stored passwords.
  web.on("/api/wifi-scan", HTTP_GET, []() {
    if (!setupMode) { web.send(403, "application/json", "{\"error\":\"setup_disabled\"}"); return; }
    touchSetup();
    // The AP stays enabled during the station scan (WIFI_AP_STA).
    int count = WiFi.scanNetworks(false, false);
    String json = "{\"networks\":[";
    String seen = "\n";
    int emitted = 0;
    for (int i = 0; i < count && emitted < 30; ++i) {
      String name = WiFi.SSID(i);
      if (!name.length() || seen.indexOf("\n" + name + "\n") >= 0) continue;
      seen += name + "\n";
      String escaped;
      for (size_t k = 0; k < name.length(); ++k) {
        char c = name[k];
        if (c == '\"' || c == '\\') escaped += '\\';
        if ((unsigned char)c < 0x20) continue;
        escaped += c;
      }
      if (emitted++) json += ',';
      json += "\"" + escaped + "\"";
    }
    WiFi.scanDelete();
    json += "]}";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json; charset=utf-8", json);
  });

  web.on("/api/setup-status", HTTP_GET, []() {
    if (!setupMode) { web.send(403, "application/json", "{\"state\":\"closed\"}"); return; }
    touchSetup();
    // No passwords or other credentials are exposed by this endpoint.
    String json = "{\"state\":\"" + provisionState + "\"";
    json += ",\"ip\":\"" + provisionIP + "\"";
    // Escape SSID for JSON; never expose Wi-Fi passwords.
    String safeSSID;
    for (size_t i = 0; i < provisionSSID.length(); ++i) {
      char c = provisionSSID[i];
      if (c == '"' || c == '\\') safeSSID += '\\';
      if ((unsigned char)c < 0x20) continue;
      safeSSID += c;
    }
    json += ",\"ssid\":\"" + safeSSID + "\"";

    json += ",\"stage\":\"" + provisionStage + "\"";
    json += ",\"error\":\"" + provisionError + "\"";
    json += ",\"host\":\"http://esp32-plant.local\"";
    json += ",\"ap_closes_in_seconds\":" + String(provisionState == "success" ?
       (millis()-provisionSuccessAt >= SETUP_SUCCESS_GRACE_MS ? 0 :
       (SETUP_SUCCESS_GRACE_MS-(millis()-provisionSuccessAt))/1000UL) : 0);
    json += "}";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", json);
  });

  web.on("/save", HTTP_POST, []() {
    if (!setupMode || changePending) { web.send(403, "text/plain", "Setup unavailable"); return; }
    touchSetup();
    newName = web.arg("ssid");
    newPass = web.arg("password");
    if (newName.length() == 0 || newName.length() > 32 ||
        (newPass.length() > 0 && newPass.length() < 8)) {
      web.send(400, "text/plain; charset=utf-8", "Wi-Fi 名稱或密碼長度不正確");
      return;
    }
    provisionState = "connecting";
    provisionStage = "sta_connect";
    provisionError = "";
    provisionIP = "";
    provisionSSID = newName;
    provisionSuccessAt = 0;
    changePending = true;
    changeAt = millis();
    // V4.29: the iOS captive-portal window may close as soon as SoftAP stops.
    // Show all final instructions BEFORE AP shutdown. Do not offer a misleading
    // button that depends on detecting the phone's current SSID.
    String html =
      "<!doctype html><html lang='zh-Hant'><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<style>body{font-family:-apple-system,BlinkMacSystemFont,Arial,sans-serif;background:#f6f8fa;color:#1f2937;margin:0;padding:18px}"
      ".card{max-width:520px;margin:10px auto;background:white;border-radius:20px;padding:25px 20px;box-shadow:0 3px 16px #0000000b}"
      "h2{font-size:25px;margin:10px 0 12px}.center{text-align:center}.check{display:inline-flex;align-items:center;justify-content:center;width:68px;height:68px;border-radius:50%;background:#25b64b;color:white;font-size:45px;font-weight:bold}"
      ".muted{color:#687585;font-size:13px}.ip{color:#88919c;font-size:12px;margin:6px 0 24px;word-break:break-all}"
      ".wifi{font-size:36px;color:#2589ef;margin:10px}.hint{background:#edf9f0;border-radius:14px;padding:18px;margin:20px 0}"
      ".ssid{font-size:28px;font-weight:750;margin:4px 0}.steps{line-height:1.8}.foot{font-size:13px;color:#687585;background:#f4f6f9;padding:14px;border-radius:12px}"
      "a{overflow-wrap:anywhere;color:#1767cb}.error{background:#fff2ed;border-radius:12px;padding:16px}</style>"
      "</head><body><main class='card'><section id='working' class='center'><div class='wifi'>&#128246;</div>"
      "<h2>正在測試 Wi-Fi 連線</h2><p>系統正在確認家用 Wi-Fi 連線，請稍候。</p></section>"
      "<section id='success' style='display:none'><div class='center'><div class='check'>&#10003;</div>"
      "<h2>設備安裝成功</h2><p>您的鹿角蕨環控設備已經成功連接家用 Wi-Fi。</p>"
      "<p class='ip'>ESP32 IP：<span id='deviceIp'></span></p>"
      "<div class='wifi'>&#128246; &#10003;</div></div>"
      "<div class='hint'><div class='muted'>請在手機 Wi-Fi 畫面選擇</div><div class='ssid' id='targetSsid'></div>"
      "<div>如手機要求密碼，請輸入您原本的家用 Wi-Fi 密碼。</div></div>"
      "<p class='steps'>完成選取後，請開啟瀏覽器（Safari 或 Google Chrome），在網址列輸入："
      "<br><strong>http://esp32-plant.local</strong></p>"
      "<p class='foot'>設定熱點將在約 30 秒後自動關閉。若手機沒有自動連回家用 Wi-Fi，請在手機「設定 → Wi-Fi」中選擇上述網路。此頁面可能隨設定熱點關閉而消失。</p>"
      "</section><section id='failure' style='display:none'><h2>Wi-Fi 連線失敗</h2>"
      "<p class='error' id='failureMessage'></p><p><a href='/setup'>重新設定 Wi-Fi</a></p></section>"
      "</main><script>let done=false;async function check(){if(done)return;try{"
      "const r=await fetch('/api/setup-status',{cache:'no-store'});if(!r.ok)throw Error('HTTP');"
      "const x=await r.json();if(x.state==='success'){done=true;"
      "document.getElementById('working').style.display='none';document.getElementById('success').style.display='block';"
      "document.getElementById('deviceIp').textContent=x.ip||'--';"
      "document.getElementById('targetSsid').textContent=x.ssid||'您選擇的家用網路';"
      "}else if(x.state==='failed'){done=true;document.getElementById('working').style.display='none';"
      "document.getElementById('failure').style.display='block';"
      "document.getElementById('failureMessage').textContent='錯誤碼：'+(x.error||'E-03')+'；階段：'+(x.stage||'sta_connect')+'。請確認家用 Wi-Fi 密碼或訊號後重試。';"
      "}}catch(e){if(!done)document.querySelector('#working p').textContent='正在等待設備回應，請保持連接 PLANT 設定熱點。';}"
      "if(!done)setTimeout(check,1500);}setTimeout(check,1200);</script></body></html>";
    web.send(200, "text/html; charset=utf-8", html);
  });

  // V4.29 captive portal: OS connectivity probes may open the setup page.
  // Never intercept normal LAN requests when setup mode is disabled.
  auto portalRedirect = []() {
    if (!setupMode) { web.send(404, "text/plain", "Not found"); return; }
    touchSetup();
    web.sendHeader("Location", "http://192.168.4.1/setup", true);
    web.sendHeader("Cache-Control", "no-store");
    web.send(302, "text/plain", "Open Wi-Fi setup");
  };
  web.on("/generate_204", HTTP_GET, portalRedirect); // Android
  web.on("/gen_204", HTTP_GET, portalRedirect);
  web.on("/hotspot-detect.html", HTTP_GET, portalRedirect); // iOS
  web.on("/library/test/success.html", HTTP_GET, portalRedirect);
  web.on("/connecttest.txt", HTTP_GET, portalRedirect); // Windows
  web.on("/ncsi.txt", HTTP_GET, portalRedirect);
  web.onNotFound(portalRedirect);
  web.begin();
}

// Shared environmental CSV and Firebase payload helpers. Legacy sync loops removed.
String historyLastError="none";
int historyLastHttpCode=0;
bool historyParseCsv(const String &line, String &day, String &recordKey,
                     time_t &epoch, String &temp, String &rh) {
  // CSV fields: timestamp, uptime_ms, temperature_c, rh_pct, ...
  int c1=line.indexOf(','); if(c1<0) return false;
  int c2=line.indexOf(',',c1+1); if(c2<0) return false;
  int c3=line.indexOf(',',c2+1); if(c3<0) return false;
  int c4=line.indexOf(',',c3+1); if(c4<0) return false;
  String stamp=line.substring(0,c1);
  temp=line.substring(c2+1,c3); rh=line.substring(c3+1,c4);
  if(stamp.length()<19 || stamp[4]!='-' || stamp[7]!='-' || stamp[10]!='T') return false;
  day=stamp.substring(0,10);
  recordKey="";
  for(int i=0;i<19;i++) if(stamp[i]>='0'&&stamp[i]<='9') recordKey+=stamp[i];
  if(recordKey.length()!=14 || temp.length()==0 || rh.length()==0) return false;
  // strtof requires entire numeric token to be valid; reject NaN, infinities and partial numbers.
  char *endT=nullptr,*endH=nullptr;
  float t=strtof(temp.c_str(),&endT), h=strtof(rh.c_str(),&endH);
  if(!endT || *endT || !endH || *endH || !isfinite(t) || !isfinite(h) ||
     t < -40 || t > 125 || h < 0 || h > 100) return false;
  struct tm parsed={};
  String localTime=stamp.substring(0,19);
  char *end=strptime(localTime.c_str(),"%Y-%m-%dT%H:%M:%S",&parsed);
  if(!end || *end) return false;
  // Convert the explicit ISO-8601 offset to UTC, independent of ESP32 TZ.
  // Logger uses YYYY-MM-DDTHH:MM:SS+0800. Never interpret wall time as UTC.
  if(stamp.length()<24 || (stamp[19]!='+' && stamp[19]!='-') ||
     stamp[22]<'0' || stamp[22]>'9') return false;
  for(int i=20;i<24;i++) if(stamp[i]<'0'||stamp[i]>'9') return false;
  int offsetHour=(stamp[20]-'0')*10+(stamp[21]-'0');
  int offsetMinute=(stamp[22]-'0')*10+(stamp[23]-'0');
  if(offsetHour>14 || offsetMinute>59) return false;
  int offsetSeconds=(offsetHour*60+offsetMinute)*60;
  if(stamp[19]=='-') offsetSeconds=-offsetSeconds;
  // Require the agreed device logging offset for Asia/Taipei.
  if(offsetSeconds!=28800) return false;
  parsed.tm_isdst=0;
  // Civil Gregorian date -> days since 1970-01-01, independent of TZ and timegm().
  // Compatible with ESP32 Arduino Core 2.0.3 (newlib without timegm declaration).
  const int year=parsed.tm_year+1900, month=parsed.tm_mon+1, date=parsed.tm_mday;
  if(year<2020 || year>2099 || month<1 || month>12 || date<1 || date>31 ||
     parsed.tm_hour>23 || parsed.tm_min>59 || parsed.tm_sec>59) return false;
  static const int mdays[]={31,28,31,30,31,30,31,31,30,31,30,31};
  const bool leap=(year%4==0 && (year%100!=0 || year%400==0));
  if(date>mdays[month-1]+((month==2 && leap)?1:0)) return false;
  int y=year-(month<=2 ? 1 : 0);
  const int era=y/400;
  const unsigned yoe=(unsigned)(y-era*400);
  const unsigned mp=(unsigned)(month+(month>2?-3:9));
  const unsigned doy=(153*mp+2)/5+(unsigned)date-1;
  const unsigned doe=yoe*365+yoe/4-yoe/100+doy;
  const int64_t days=(int64_t)era*146097+(int64_t)doe-719468;
  const int64_t utc=days*86400LL+parsed.tm_hour*3600LL+parsed.tm_min*60LL+parsed.tm_sec-offsetSeconds;
  if(utc<1700000000LL || utc>4102444800LL) return false;
  epoch=(time_t)utc;
  return epoch >= 1700000000;
}

bool historyPut(const String &day, const String &key, time_t epoch,
                const String &temp, const String &rh) {
  WiFiClientSecure client;
  client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  String url=String("https://")+FIREBASE_DB_HOST+
      "/devices/esp32-s3/history/"+day+"/"+key+".json?auth="+firebaseIdToken;
  if(!http.begin(client,url)) {historyLastError="HTTPS 初始化失敗";return false;}
  http.setTimeout(5000);
  http.addHeader("Content-Type","application/json");
  String payload=String("{\"timestamp\":")+String((long)epoch)+
      ",\"temperature_c\":"+temp+",\"humidity_rh\":"+rh+"}";
  historyLastHttpCode=http.PUT(payload);
  String reply=http.getString();
  http.end();
  if(historyLastHttpCode==200) {historyLastError="無";return true;}
  historyLastError=String("HTTP ")+historyLastHttpCode+" "+firebaseSafeError(reply);
  if(historyLastHttpCode==401 || historyLastHttpCode==403) firebaseIdToken="";
  return false;
}

// Persist a committed cursor as a date + byte offset. A record is committed
// only AFTER Firebase HTTP 200. Retrying a committed PUT is idempotent.
bool historyLoadCursor(String &day, uint32_t &offset, bool &ready) {
  Preferences hist;
  if (!hist.begin("histv424", false)) return false;
  ready = hist.getBool("ready", false);
  day = hist.getString("day", "");
  offset = hist.getUInt("offset", 0);
  hist.end();
  return true;
}

String historyLocalDay(time_t epoch) {
  struct tm local = {};
  if (!localtime_r(&epoch, &local)) return "";
  char date[16];
  if (!strftime(date, sizeof(date), "%Y-%m-%d", &local)) return "";
  return String(date);
}

String historyNextDay(const String &day) {
  struct tm tmDay = {};
  String stamp = day + "T12:00:00";
  if (!strptime(stamp.c_str(), "%Y-%m-%dT%H:%M:%S", &tmDay)) return "";
  tmDay.tm_isdst = -1;
  time_t next = mktime(&tmDay) + 86400;
  return historyLocalDay(next);
}


// V4.25 integrated PAR SD audit + Firebase backfill, independent of V4.24 history.
// Firebase RTDB creates JSON nodes automatically when authenticated rules allow PUT.
const uint32_t PAR_SYNC_INTERVAL425=4500UL;
const uint32_t PAR_SYNC_RETRY425=30000UL;
unsigned long parSyncAttempt425=0;
uint32_t parSyncGap425=PAR_SYNC_INTERVAL425;
uint32_t parSyncSuccess425=0, parSyncRejected425=0;
int parSyncHttp425=0;
String parSyncState425="PENDING", parSyncError425="none", parSyncDate425="", parSyncKey425="";
String parSyncNode425="/devices/esp32-s3/par_samples";
String verifyParDay425="",verifyParKey425="";
uint32_t verifyParStart425=0;
String verifyEnvDay425="",verifyEnvKey425="";
uint32_t verifyEnvStart425=0;

// Read-only Firebase probe, never writes test data or exposes ID tokens.
int parProbeHttp425=0;
String parProbeState425="NOT_RUN", parProbeError425="none";
uint32_t parProbeAt425=0;
String parPayloadShape425="timestamp:number,uptime_ms:number,ppfd_valid:boolean,ppfd_umol_m2_s:number-or-null";
String parWriteTarget425="/devices/esp32-s3/par_samples/$date/$record";
String parLastWriteErrorClass425="none";

void parProbeRead425(const String &day, const String &key) {
  if (!firebaseIdToken.length()) {parProbeState425="NO_TOKEN";return;}
  // One GET per 60 seconds at most, to avoid disturbing existing sync timing.
  if(parProbeAt425 && millis()-parProbeAt425<60000UL)return;
  parProbeAt425=millis();
  WiFiClientSecure client;client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  String url="https://"+String(FIREBASE_DB_HOST)+parSyncNode425+"/"+day+"/"+key+".json?auth="+firebaseIdToken;
  if(!http.begin(client,url)){parProbeState425="HTTPS_BEGIN_FAIL";return;}
  http.setTimeout(5000);
  parProbeHttp425=http.GET();
  String body=http.getString();http.end();
  if(parProbeHttp425==200 && body.indexOf("\"timestamp\"")>=0 && body.indexOf("\"ppfd_valid\"")>=0 && body.indexOf("\"uptime_ms\"")>=0){parProbeState425="READ_FIELDS_PRESENT";parProbeError425="none";}
  else if(parProbeHttp425==200){parProbeState425="READ_EMPTY_OR_FIELDS_MISSING";parProbeError425="missing required fields";}
  else {parProbeState425="READ_FAILED";parProbeError425=firebaseSafeError(body);}
}


bool parLoadCursor425(String &day,uint32_t &offset,bool &ready) {
  // First boot: the PAR cursor namespace may not exist yet. Read-write mode creates it.
  // Do not clear NVS or alter the existing V4.24 history cursor.
  Preferences p; if(!p.begin("parsync425",false))return false;
  ready=p.getBool("ready",false); day=p.getString("day","");offset=p.getUInt("offset",0);p.end();return true;
}
bool parSaveCursor425(const String &day,uint32_t offset) {
  Preferences p;if(!p.begin("parsync425",false))return false;
  bool ok=p.putUInt("offset",offset)==sizeof(uint32_t);
  if(ok)ok=p.putString("day",day)==day.length();
  if(ok)ok=p.putBool("ready",true)==sizeof(uint8_t);
  p.end();return ok;
}
// Timestamp is ISO8601 with timezone, epoch is canonical and supplies stable keys.
bool parParse425(const String &line,uint32_t &epoch,uint32_t &uptime,
                 float &ppfd,bool &valid) {
  String cols[8];int pos=0;
  for(int i=0;i<7;i++){int n=line.indexOf(',',pos);if(n<0)return false;cols[i]=line.substring(pos,n);pos=n+1;}
  cols[7]=line.substring(pos);cols[7].trim();
  if(cols[0].length()<19 || cols[0][4]!='-' || cols[0][10]!='T')return false;
  char *end=nullptr;unsigned long e=strtoul(cols[1].c_str(),&end,10);
  if(!end || *end || e<1700000000UL || e>4102444800UL)return false;
  epoch=(uint32_t)e;
  unsigned long up=strtoul(cols[2].c_str(),&end,10);
  if(!end || *end)return false;uptime=(uint32_t)up;
  if(cols[4]!="0" && cols[4]!="1")return false;
  if(cols[6]!="0" && cols[6]!="1")return false;
  if(cols[4]!=cols[6])return false;
  valid=cols[6]=="1";ppfd=0;
  if(valid){ppfd=strtof(cols[5].c_str(),&end);
    if(!end||*end||!isfinite(ppfd)||ppfd<0||ppfd>65535)return false;
    unsigned long raw=strtoul(cols[3].c_str(),&end,10);
    if(!end||*end||raw>65535||fabsf(ppfd-float(raw))>0.01f)return false;
  }
  return true;
}
// Scan current day's physical file, not just RAM queue counters.
String parAuditJson425(){
  time_t now=time(nullptr);String day=now>=1700000000?historyLocalDay(now):"";
  String path="/logs/par_samples_"+day+".csv";
  uint32_t rows=0,bad=0,duplicate=0,gaps=0,validRows=0,bytes=0,lastEpoch=0;
  float lastPpfd=0;bool lastValid=false;String state="PENDING";
  if(!sdInitOK)state="SD_NOT_MOUNTED";
  else if(!day.length())state="TIME_NOT_READY";
  else if(!SD.exists(path))state="FILE_MISSING";
  else {
    File f=SD.open(path,FILE_READ);
    if(!f)state="OPEN_FAILED";
    else {
      bytes=f.size();String header=f.readStringUntil('\n');header.trim();
      if(!header.startsWith("timestamp,epoch,uptime_ms,par_raw_reg0"))state="BAD_HEADER";
      else {
        while(f.available()){
          String line=f.readStringUntil('\n');line.trim();if(!line.length())continue;
          uint32_t epoch=0,uptime=0;float ppfd=0;bool valid=false;
          if(!parParse425(line,epoch,uptime,ppfd,valid)){bad++;continue;}
          rows++;if(valid)validRows++;
          if(lastEpoch && epoch<=lastEpoch)duplicate++;
          if(lastEpoch && epoch>lastEpoch+15)gaps++;
          lastEpoch=epoch;lastPpfd=ppfd;lastValid=valid;
          if((rows+bad)%64==0)yield();
        }
        state=(bad||duplicate||parQueueOverflow425)?"WARNING":"PASS";
      }
      f.close();
    }
  }
  String j="{\"file\":\""+path+"\",\"sd_state\":\""+state+"\"";
  j+=",\"file_bytes\":"+String(bytes)+",\"rows\":"+String(rows)+",\"valid_rows\":"+String(validRows);
  j+=",\"bad_rows\":"+String(bad)+",\"duplicate_or_reverse\":"+String(duplicate);
  j+=",\"sampling_gaps_gt15s\":"+String(gaps)+",\"last_epoch\":"+String(lastEpoch);
  j+=",\"last_ppfd\":"+(lastValid?String(lastPpfd,2):"null");
  j+=",\"queue_pending\":"+String(parCount425)+",\"queue_overflow\":"+String(parQueueOverflow425);
  j+=",\"sd_failures\":"+String(parSdFailures425)+"}";return j;
}
void updateParFirebase425(){
  if(setupMode||changePending||WiFi.status()!=WL_CONNECTED||!sdInitOK||
     !firebaseConfigured()||!firebaseIdToken.length())return;
  if(parSyncAttempt425 && millis()-parSyncAttempt425<parSyncGap425)return;
  parSyncAttempt425=millis();
  time_t now=time(nullptr);if(now<1700000000){parSyncState425="TIME_NOT_READY";return;}
  String today=historyLocalDay(now),day;uint32_t offset=0;bool ready=false;
  if(!parLoadCursor425(day,offset,ready)){parSyncState425="NVS_OPEN_FAIL";return;}
  if(!ready){
    // First sync begins at today's file start, not EOF: no silent loss of prior samples.
    day=today;offset=0;
    if(!parSaveCursor425(day,offset)){parSyncState425="NVS_INIT_FAIL";return;}
  }
  if(day.length()!=10||day>today){parSyncState425="BAD_CURSOR_DATE";return;}
  String path="/logs/par_samples_"+day+".csv";
  if(!SD.exists(path)){
    if(day==today){parSyncState425="WAITING_FOR_PAR_FILE";return;}
    String next=historyNextDay(day);
    if(next.length() && next>day && parSaveCursor425(next,0))parSyncState425="NEXT_DAY";
    else parSyncState425="NEXT_DAY_NVS_FAIL";
    return;
  }
  File f=SD.open(path,FILE_READ);if(!f){parSyncState425="SD_OPEN_FAIL";return;}
  uint32_t size=f.size();
  if(offset>size){f.close();parSyncState425="CURSOR_BEYOND_EOF";return;}
  if(!f.seek(offset)){f.close();parSyncState425="SD_SEEK_FAIL";return;}
  if(offset==0){
    String header=f.readStringUntil('\n');header.trim();
    if(!header.startsWith("timestamp,epoch,uptime_ms,par_raw_reg0")){
      f.close();parSyncState425="BAD_HEADER";return;
    }
    uint32_t next=f.position();
    if(!parSaveCursor425(day,next)){f.close();parSyncState425="HEADER_CURSOR_FAIL";return;}
    offset=next;
  }
  if(!f.available()){
    f.close();if(day!=today){String next=historyNextDay(day);
      parSyncState425=(next.length()&&next>day&&parSaveCursor425(next,0))?"NEXT_DAY":"NEXT_DAY_FAIL";
    }else parSyncState425="PASS_CAUGHT_UP";
    return;
  }
  String line=f.readStringUntil('\n');uint32_t next=f.position();
  if(next==size && size && (f.seek(size-1),f.read()!='\n')){
    f.close();parSyncState425="WAITING_COMPLETE_LINE";return;
  }
  f.close();
  uint32_t epoch=0,uptime=0;float ppfd=0;bool valid=false;
  if(!parParse425(line,epoch,uptime,ppfd,valid)){
    // Never silently skip corrupt rows: retain cursor for investigation.
    ++parSyncRejected425;parSyncState425="INVALID_CSV_STOP";
    parSyncError425="byte offset "+String(offset);parSyncGap425=PAR_SYNC_RETRY425;return;
  }
  // Epoch plus uptime disambiguates two samples in the same second.
  String key=String(epoch)+"_"+String(uptime);
  WiFiClientSecure client;client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  String url="https://"+String(FIREBASE_DB_HOST)+parSyncNode425+"/"+day+"/"+key+".json?auth="+firebaseIdToken;
  if(!http.begin(client,url)){parSyncState425="HTTPS_BEGIN_FAIL";parSyncGap425=PAR_SYNC_RETRY425;return;}
  http.setTimeout(5000);http.addHeader("Content-Type","application/json");
  String payload="{\"timestamp\":"+String(epoch)+",\"uptime_ms\":"+String(uptime)+
    ",\"ppfd_valid\":"+(valid?"true":"false")+
    ",\"ppfd_umol_m2_s\":"+(valid?String(ppfd,2):"null")+"}";
  parSyncHttp425=http.PUT(payload);String response=http.getString();http.end();
  parSyncDate425=day;parSyncKey425=key;
  if(parSyncHttp425==200){
    if(parSaveCursor425(day,next)){
      ++parSyncSuccess425;verifyParDay425=day;verifyParKey425=key;verifyParStart425=offset;parSyncState425="PASS_UPLOADED";
      parSyncError425="none";parLastWriteErrorClass425="none";parSyncGap425=PAR_SYNC_INTERVAL425;
    }else{parSyncState425="NVS_COMMIT_FAIL_RETRY";parSyncGap425=PAR_SYNC_RETRY425;}
  }else{
    parSyncState425="FIREBASE_PUT_FAIL";
    parSyncError425="HTTP "+String(parSyncHttp425)+" "+firebaseSafeError(response);
    parLastWriteErrorClass425=(parSyncHttp425==401||parSyncHttp425==403)?
      "AUTH_OR_RULES_DENIED":(parSyncHttp425==400?"BAD_REQUEST_OR_VALIDATION":"HTTP_OR_NETWORK_FAILURE");
    parSyncGap425=PAR_SYNC_RETRY425;
    if(parSyncHttp425==401||parSyncHttp425==403)firebaseIdToken="";
  }
}
String parSyncJson425(){
  String day;uint32_t offset=0;bool ready=false;parLoadCursor425(day,offset,ready);
  String j="{\"firmware\":\""+String(FW_VERSION)+"\",\"micro_sd\":"+parAuditJson425();
  j+=",\"firebase\":{\"state\":\""+parSyncState425+"\",\"node\":\""+parSyncNode425+"\"";
  j+=",\"cursor_date\":\""+day+"\",\"cursor_offset\":"+String(offset);
  j+=",\"cursor_ready\":"+String(ready?"true":"false")+",\"uploaded_this_boot\":"+String(parSyncSuccess425);
  j+=",\"rejected_rows\":"+String(parSyncRejected425)+",\"last_http\":"+String(parSyncHttp425);
  j+=",\"last_date\":\""+parSyncDate425+"\",\"last_key\":\""+parSyncKey425+"\"";
  j+=",\"last_error\":\""+parSyncError425+"\"";
  j+=",\"read_probe_http\":"+String(parProbeHttp425);
  j+=",\"read_probe_state\":\""+parProbeState425+"\"";
  j+=",\"read_probe_error\":\""+parProbeError425+"\"";
  j+=",\"write_error_class\":\""+parLastWriteErrorClass425+"\"";
  j+=",\"write_target_pattern\":\""+parWriteTarget425+"\"";
  j+=",\"payload_fields\":\""+parPayloadShape425+"\"";
  j+=",\"token_present\":"+String(firebaseIdToken.length()?"true":"false");
  j+=",\"firebase_login_http\":"+String(firebaseAuthHttpCode);
  j+="}}";return j;
}

// Read-only migration from the prior envbf425 NVS cursor; no old upload loop.
bool envBackfillCursor425(const String &day,uint32_t &offset,bool save){
  if(save)return false;
  Preferences p;if(!p.begin("envbf425",true))return false;
  String stored=p.getString("day","");offset=stored==day?p.getUInt("offset",0):0;
  p.end();return true;
}
void updateParFirebase425();
String parSyncJson425();
// V4.25 unified sync scheduler (development candidate).
// Sensors retain their own CSV parser and Firebase payload. Shared policy is
// current-day-first, background historical catch-up, durable cursors, retry.
// The former histv424 and envbf425 functions remain below ONLY for migration
// reference; the unified scheduler does not invoke either one.
uint32_t unifiedTick425=0, unifiedCycle425=0;
String unifiedState425="PENDING";
uint32_t unifiedLiveSuccess425=0, unifiedBackSuccess425=0;
String unifiedLiveError425="none", unifiedBackError425="none";
uint32_t unifiedLastLiveEpoch425=0;
uint32_t unifiedLastBackEpoch425=0;

bool unifiedCursor425(const char *ns, const String &day, uint32_t &offset, bool save) {
  Preferences p;
  if(!p.begin(ns,false))return false;
  bool ok=true;
  if(save){
    ok=p.putUInt("offset",offset)==sizeof(uint32_t);
    if(ok)ok=p.putString("day",day)==day.length();
  }else{
    String stored=p.getString("day","");
    offset=stored==day?p.getUInt("offset",0):0;
  }
  p.end();return ok;
}

// Read one complete environmental CSV row at the cursor; no network here.
// Returns 1=row, 0=end/wait, -1=invalid/error. Cursor is not committed.
int unifiedReadEnv425(const String &day,uint32_t offset,uint32_t &next,
                      String &key,time_t &epoch,String &temp,String &rh) {
  String path="/logs/"+day+".csv";
  if(!SD.exists(path))return 0;
  File f=SD.open(path,FILE_READ);if(!f)return -1;
  uint32_t size=f.size();if(offset>size){f.close();return -1;}
  if(!f.seek(offset)){f.close();return -1;}
  if(offset==0){String header=f.readStringUntil('\n');
    if(!header.startsWith("timestamp,")){f.close();return -1;}}
  if(!f.available()){f.close();return 0;}
  String line=f.readStringUntil('\n');next=f.position();
  if(next==size && size){f.seek(size-1);if(f.read()!='\n'){f.close();return 0;}}
  f.close();String parsedDay;
  if(!historyParseCsv(line,parsedDay,key,epoch,temp,rh)||parsedDay!=day)return -1;
  return 1;
}

// One current-day environment record per scheduled slot, beginning at the
// existing histv424 cursor if present, otherwise at the start of today's CSV.
void unifiedLiveEnv425(const String &today){
  uint32_t offset=0;
  if(!unifiedCursor425("envlive425",today,offset,false)){
    unifiedLiveError425="NVS_READ_FAIL";return;
  }
  // Preserve already acknowledged realtime progress from V4.24 during migration.
  if(offset==0){String oldDay;uint32_t oldOffset=0;bool ready=false;
    if(historyLoadCursor(oldDay,oldOffset,ready)&&ready&&oldDay==today){
      offset=oldOffset;
      if(!unifiedCursor425("envlive425",today,offset,true)){
        unifiedLiveError425="MIGRATE_CURSOR_FAIL";return;
      }
    }
  }
  uint32_t next=0;String key,temp,rh;time_t epoch=0;
  int r=unifiedReadEnv425(today,offset,next,key,epoch,temp,rh);
  if(r==0){unifiedLiveError425="none";return;}
  if(r<0){unifiedLiveError425="INVALID_OR_SD_ERROR_AT_"+String(offset);return;}
  if(!historyPut(today,key,epoch,temp,rh)){
    unifiedLiveError425=historyLastError;return;
  }
  if(!unifiedCursor425("envlive425",today,next,true)){
    unifiedLiveError425="NVS_COMMIT_FAIL_RETRY";return;
  }
  ++unifiedLiveSuccess425;verifyEnvDay425=today;verifyEnvKey425=key;verifyEnvStart425=offset;unifiedLastLiveEpoch425=(uint32_t)epoch;
  unifiedLiveError425="none";
}

// Historical lane: follows the oldest outstanding date without resetting at
// midnight. Historical start date is seeded from the current date on first use.
void unifiedBackEnv425(const String &today){
  Preferences p;if(!p.begin("envback425",false)){
    unifiedBackError425="NVS_OPEN_FAIL";return;
  }
  String day=p.getString("day","");uint32_t offset=p.getUInt("offset",0);
  p.end();
  if(!day.length()){
    // Migration: existing envbf425 progress, when available.
    uint32_t oldOffset=0;
    if(!envBackfillCursor425(today,oldOffset,false)){
      unifiedBackError425="MIGRATE_READ_FAIL";return;
    }
    day=today;offset=oldOffset;
    if(!unifiedCursor425("envback425",day,offset,true)){
      unifiedBackError425="MIGRATE_WRITE_FAIL";return;
    }
  }
  if(day>today || day.length()!=10){unifiedBackError425="BAD_DATE";return;}
  uint32_t next=0;String key,temp,rh;time_t epoch=0;
  int r=unifiedReadEnv425(day,offset,next,key,epoch,temp,rh);
  if(r==0){
    if(day<today){
      String tomorrow=historyNextDay(day);uint32_t zero=0;
      if(tomorrow.length()==10 && tomorrow<=today &&
         unifiedCursor425("envback425",tomorrow,zero,true))
         unifiedBackError425="ADVANCED_DAY";
      else unifiedBackError425="NEXT_DAY_FAIL";
    }else unifiedBackError425="none";
    return;
  }
  if(r<0){unifiedBackError425="INVALID_OR_SD_ERROR_AT_"+String(offset);return;}
  if(!historyPut(day,key,epoch,temp,rh)){
    unifiedBackError425=historyLastError;return;
  }
  if(!unifiedCursor425("envback425",day,next,true)){
    unifiedBackError425="NVS_COMMIT_FAIL_RETRY";return;
  }
  ++unifiedBackSuccess425;unifiedLastBackEpoch425=(uint32_t)epoch;
  unifiedBackError425="none";
}

void updateUnifiedSync425(){
  if(setupMode||changePending||WiFi.status()!=WL_CONNECTED||
     !sdInitOK||!firebaseIdToken.length()||!firebaseConfigured())return;
  if(unifiedTick425 && millis()-unifiedTick425<3000UL)return;
  unifiedTick425=millis();
  time_t now=time(nullptr);if(now<1700000000)return;
  String today=historyLocalDay(now);if(today.length()!=10)return;
  // Six slots: three current-day environment, two PAR, one old environment.
  // PAR retains its verified durable cursor and parser.
  uint8_t slot=(unifiedCycle425++)%6;
  if(slot==0||slot==2||slot==4){unifiedLiveEnv425(today);unifiedState425="LIVE_ENV";}
  else if(slot==1||slot==3){updateParFirebase425();unifiedState425="PAR";}
  else {unifiedBackEnv425(today);unifiedState425="BACKGROUND_ENV";}
}
String unifiedSyncJson425(){
  String today=historyLocalDay(time(nullptr));uint32_t live=0,back=0;
  unifiedCursor425("envlive425",today,live,false);
  Preferences p;String backDay="";
  if(p.begin("envback425",true)){backDay=p.getString("day","");back=p.getUInt("offset",0);p.end();}
  uint32_t size=0;String path="/logs/"+today+".csv";
  if(sdInitOK&&SD.exists(path)){File f=SD.open(path,FILE_READ);if(f){size=f.size();f.close();}}
  String j="{\"firmware\":\""+String(FW_VERSION)+"\",\"scheduler\":\""+unifiedState425+"\"";
  j+=",\"environment\":{\"today\":\""+today+"\",\"file_bytes\":"+String(size);
  j+=",\"live_cursor\":"+String(live)+",\"live_uploaded_this_boot\":"+String(unifiedLiveSuccess425);
  j+=",\"live_last_epoch\":"+String(unifiedLastLiveEpoch425)+",\"live_error\":\""+unifiedLiveError425+"\"";
  j+=",\"backfill_day\":\""+backDay+"\",\"backfill_cursor\":"+String(back);
  j+=",\"backfill_uploaded_this_boot\":"+String(unifiedBackSuccess425);
  j+=",\"backfill_last_epoch\":"+String(unifiedLastBackEpoch425)+",\"backfill_error\":\""+unifiedBackError425+"\"}";
  j+=",\"par\":"+parSyncJson425()+"}";return j;
}


// One-shot SD vs Firebase content validation. Only runs when user opens
// /api/verify-sync; never invoked by the scheduler. No secrets in JSON output.
// Uses the latest acknowledged current-day records in RAM and re-reads the
// corresponding physical SD row, rather than comparing against upload RAM.
String verifyJsonNumber425(const String &json,const char *field){
  String pattern="\""+String(field)+"\"";
  int at=json.indexOf(pattern);if(at<0)return "";
  at=json.indexOf(':',at+pattern.length());if(at<0)return "";
  ++at;while(at<(int)json.length() && (json[at]==' '||json[at]=='\n'))++at;
  int end=at;while(end<(int)json.length() &&
      ((json[end]>='0'&&json[end]<='9')||json[end]=='-'||json[end]=='+'||json[end]=='.'||json[end]=='e'||json[end]=='E'))++end;
  return json.substring(at,end);
}
bool verifyNumEq425(const String &actual,const String &expected,double tol){
  if(!actual.length()||!expected.length())return false;
  char *a=nullptr,*b=nullptr;
  double x=strtod(actual.c_str(),&a),y=strtod(expected.c_str(),&b);
  return a&&b&&*a==0&&*b==0&&isfinite(x)&&isfinite(y)&&fabs(x-y)<=tol;
}
String verifyFetch425(const String &path,int &code){
  WiFiClientSecure client;client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  String url="https://"+String(FIREBASE_DB_HOST)+path+".json?auth="+firebaseIdToken;
  if(!http.begin(client,url)){code=-1;return "";}
  http.setTimeout(5000);code=http.GET();
  String body=http.getString();http.end();return body;
}
String verifyEnvOnce425(){
  if(!verifyEnvKey425.length())return "{\"state\":\"NO_ACKNOWLEDGED_SAMPLE_SINCE_BOOT\"}";
  String path="/logs/"+verifyEnvDay425+".csv";
  File f=SD.open(path,FILE_READ);
  if(!f)return "{\"state\":\"SD_OPEN_FAIL\"}";
  if(!f.seek(verifyEnvStart425)){f.close();return "{\"state\":\"SD_SEEK_FAIL\"}";}
  String line=f.readStringUntil('\n');f.close();
  String day,key,temp,rh;time_t epoch=0;
  if(!historyParseCsv(line,day,key,epoch,temp,rh)||day!=verifyEnvDay425||key!=verifyEnvKey425)
    return "{\"state\":\"SD_ROW_MISMATCH\"}";
  int http=0;
  String remote=verifyFetch425("/devices/esp32-s3/history/"+day+"/"+key,http);
  String stamp=line.substring(0,line.indexOf(','));
  bool sdOffsetOK=stamp.endsWith("+0800");
  struct tm localCheck={}; time_t localEpoch=epoch;
  localtime_r(&localEpoch,&localCheck);
  char expectedStamp[36]={0};
  strftime(expectedStamp,sizeof(expectedStamp),"%Y-%m-%dT%H:%M:%S%z",&localCheck);
  bool localClockOK=sdOffsetOK && stamp==String(expectedStamp);
  bool ok=localClockOK && http==200 && verifyNumEq425(verifyJsonNumber425(remote,"timestamp"),String((long)epoch),0) &&
    verifyNumEq425(verifyJsonNumber425(remote,"temperature_c"),temp,0.011) &&
    verifyNumEq425(verifyJsonNumber425(remote,"humidity_rh"),rh,0.011);
  return "{\"state\":\""+String(ok?"PASS_EXACT_FIELDS":"FAIL")+"\",\"http\":"+String(http)+
    ",\"day\":\""+day+"\",\"key\":\""+key+"\",\"sd_timestamp\":\""+stamp+
    "\",\"utc_epoch\":"+String((long)epoch)+",\"sd_local_offset_valid\":"+String(localClockOK?"true":"false")+
    ",\"fields\":\"timestamp,temperature_c,humidity_rh\"}";
}
String verifyParOnce425(){
  if(!verifyParKey425.length())return "{\"state\":\"NO_ACKNOWLEDGED_SAMPLE_SINCE_BOOT\"}";
  String path="/logs/par_samples_"+verifyParDay425+".csv";
  File f=SD.open(path,FILE_READ);
  if(!f)return "{\"state\":\"SD_OPEN_FAIL\"}";
  if(!f.seek(verifyParStart425)){f.close();return "{\"state\":\"SD_SEEK_FAIL\"}";}
  String line=f.readStringUntil('\n');f.close();
  uint32_t epoch=0,uptime=0;float ppfd=0;bool valid=false;
  if(!parParse425(line,epoch,uptime,ppfd,valid)||
      String(epoch)+"_"+String(uptime)!=verifyParKey425)
    return "{\"state\":\"SD_ROW_MISMATCH\"}";
  int http=0;
  String remote=verifyFetch425(parSyncNode425+"/"+verifyParDay425+"/"+verifyParKey425,http);
  bool remoteValid=remote.indexOf("\"ppfd_valid\":true")>=0;
  bool remoteFalse=remote.indexOf("\"ppfd_valid\":false")>=0;
  bool ppfdOk=valid?verifyNumEq425(verifyJsonNumber425(remote,"ppfd_umol_m2_s"),String(ppfd,2),0.011):
    remote.indexOf("\"ppfd_umol_m2_s\":null")>=0;
  bool ok=http==200 && verifyNumEq425(verifyJsonNumber425(remote,"timestamp"),String(epoch),0) &&
    verifyNumEq425(verifyJsonNumber425(remote,"uptime_ms"),String(uptime),0) &&
    ((valid&&remoteValid)||(!valid&&remoteFalse)) && ppfdOk;
  return "{\"state\":\""+String(ok?"PASS_EXACT_FIELDS":"FAIL")+"\",\"http\":"+String(http)+
    ",\"day\":\""+verifyParDay425+"\",\"key\":\""+verifyParKey425+
    "\",\"fields\":\"timestamp,uptime_ms,ppfd_valid,ppfd_umol_m2_s\"}";
}
String verifyBothOnce425(){
  if(!sdInitOK||WiFi.status()!=WL_CONNECTED||!firebaseIdToken.length())
    return "{\"state\":\"NOT_READY\"}";
  String env=verifyEnvOnce425(),par=verifyParOnce425();
  bool ok=env.indexOf("PASS_EXACT_FIELDS")>=0 && par.indexOf("PASS_EXACT_FIELDS")>=0;
  return "{\"firmware\":\""+String(FW_VERSION)+"\",\"state\":\""+
    String(ok?"PASS_BOTH":"INCOMPLETE_OR_FAIL")+"\",\"environment\":"+env+",\"par\":"+par+"}";
}


// V4.26 read-only historical investigation: 3 days x 9 hours (00:00-08:59).
// No SD writes, Firebase PUTs, cursor resets or synthetic samples.
struct AuditDay426 {
  String date;
  uint32_t sdRows[9]={0}, sdInvalid=0, sdOtherHours=0, sdFileBytes=0;
  uint32_t fbRows[9]={0};
  int fbHttp[9]={0};
  String fbError[9];
  String firstStamp="", lastStamp="";
  bool sdExists=false, sdOpened=false;
};
AuditDay426 audit426[3];
int auditStep426=-1;
bool auditActive426=false, auditDone426=false;
unsigned long auditLast426=0;
String auditState426="NOT_STARTED";
String auditEscape426(String s){s.replace("\\","\\\\");s.replace("\"","\\\"");s.replace("\n"," ");s.replace("\r"," ");return s;}
void auditSdDay426(AuditDay426 &a){
  String path="/logs/"+a.date+".csv";
  a.sdExists=sdInitOK&&SD.exists(path);
  if(!a.sdExists)return;
  File f=SD.open(path,FILE_READ);if(!f)return;
  a.sdOpened=true;a.sdFileBytes=f.size();
  bool first=true;
  while(f.available()){
    String line=f.readStringUntil('\n');
    if(first){first=false;continue;}
    if(!line.length())continue;
    String day,key,t,rh;time_t epoch=0;
    if(!historyParseCsv(line,day,key,epoch,t,rh)||day!=a.date){a.sdInvalid++;continue;}
    String stamp=line.substring(0,19);
    if(!a.firstStamp.length())a.firstStamp=stamp;
    a.lastStamp=stamp;
    int hour=stamp.substring(11,13).toInt();
    if(hour>=0&&hour<9)a.sdRows[hour]++;else a.sdOtherHours++;
    if((millis()&127)==0)yield();
  }
  f.close();
}
void auditStart426(){
  auditActive426=true;auditDone426=false;auditStep426=0;auditState426="RUNNING";
  time_t now=time(nullptr);
  String today=historyLocalDay(now);
  // Use local noon to avoid timezone boundary errors in date subtraction.
  struct tm t={};String noon=today+"T12:00:00";
  if(today.length()!=10||!strptime(noon.c_str(),"%Y-%m-%dT%H:%M:%S",&t)){
    auditActive426=false;auditState426="CLOCK_INVALID";return;
  }
  t.tm_isdst=-1;time_t base=mktime(&t);
  for(int i=0;i<3;i++){
    audit426[i]=AuditDay426();
    audit426[i].date=historyLocalDay(base-(2-i)*86400);
    auditSdDay426(audit426[i]);
  }
}
// Count JSON objects by the mandatory timestamp field. Query keys by local
// YYYYMMDDHH prefix; no full-day JSON is buffered in RAM.
void auditFirebaseHour426(int d,int h){
  AuditDay426 &a=audit426[d];
  if(WiFi.status()!=WL_CONNECTED){a.fbHttp[h]=-10;a.fbError[h]="WIFI_DISCONNECTED";return;}
  if(!firebaseIdToken.length()){a.fbHttp[h]=-11;a.fbError[h]="NO_FIREBASE_TOKEN";return;}
  String prefix=a.date;prefix.replace("-","");
  String hh=h<10?"0"+String(h):String(h);
  String lo=prefix+hh+"0000",hi=prefix+hh+"5959";
  String url="https://"+String(FIREBASE_DB_HOST)+"/devices/esp32-s3/history/"+a.date+
    ".json?auth="+firebaseIdToken+"&orderBy=%22%24key%22&startAt=%22"+lo+"%22&endAt=%22"+hi+"%22";
  WiFiClientSecure client;client.setCACert(FIREBASE_ROOT_CAS);
  HTTPClient http;
  if(!http.begin(client,url)){a.fbHttp[h]=-12;a.fbError[h]="HTTPS_BEGIN_FAIL";return;}
  http.setTimeout(7000);
  int code=http.GET();a.fbHttp[h]=code;
  if(code!=200){String msg=http.getString();a.fbError[h]=firebaseSafeError(msg);http.end();return;}
  WiFiClient *stream=http.getStreamPtr();
  const char *needle="\"timestamp\"";const int n=11;
  int matched=0;uint32_t count=0,bytes=0;
  unsigned long last=millis();
  while(http.connected() || stream->available()){
    if(stream->available()){
      int ch=stream->read();bytes++;last=millis();
      if(ch==needle[matched]){matched++;if(matched==n){count++;matched=0;}}
      else matched=(ch==needle[0])?1:0;
    }else{
      if(millis()-last>7000){a.fbError[h]="STREAM_TIMEOUT";break;}
      delay(1);
    }
    if(bytes>150000){a.fbError[h]="RESPONSE_LIMIT";break;}
  }
  a.fbRows[h]=count;
  http.end();
}
void auditAdvance426(){
  if(!auditActive426||auditDone426)return;
  if(auditLast426&&millis()-auditLast426<2500UL)return;
  auditLast426=millis();
  int d=auditStep426/9,h=auditStep426%9;
  if(d>=3){auditDone426=true;auditState426="COMPLETE";return;}
  auditFirebaseHour426(d,h);
  auditStep426++;
  if(auditStep426>=27){auditDone426=true;auditState426="COMPLETE";}
}
String auditReport426(){
  String j="{\"firmware\":\""+String(FW_VERSION)+"\",\"state\":\""+auditState426+
    "\",\"step\":"+String(auditStep426)+",\"total_steps\":27"+
    ",\"timezone\":\"Asia/Taipei\",\"read_only\":true,\"days\":[";
  for(int d=0;d<3;d++){
    if(d)j+=",";
    AuditDay426 &a=audit426[d];
    j+="{\"date\":\""+a.date+"\",\"sd_exists\":"+String(a.sdExists?"true":"false")+
      ",\"sd_opened\":"+String(a.sdOpened?"true":"false")+
      ",\"sd_file_bytes\":"+String(a.sdFileBytes)+
      ",\"sd_invalid_rows\":"+String(a.sdInvalid)+
      ",\"sd_other_hours\":"+String(a.sdOtherHours)+
      ",\"first_sd_stamp\":\""+a.firstStamp+"\",\"last_sd_stamp\":\""+a.lastStamp+"\",\"hours\":[";
    for(int h=0;h<9;h++){
      if(h)j+=",";
      j+="{\"hour\":"+String(h)+",\"sd\":"+String(a.sdRows[h])+
        ",\"firebase\":"+String(a.fbRows[h])+
        ",\"http\":"+String(a.fbHttp[h])+
        ",\"error\":\""+auditEscape426(a.fbError[h])+"\"}";
    }
    j+="]}";
  }
  j+="]}";return j;
}

void setup() {
  Serial.begin(115200);
  Serial.print("ESP32-S3 START: ");
  Serial.println(FW_VERSION);
  Serial.println(identitySelfTestPassed() ? "HMAC_SELF_TEST=PASS" : "HMAC_SELF_TEST=FAIL");
  identity_loadProvisionedKey();
  Serial.println("IDENTITY: ID, TEST, STATUS, PROVISION, AUTH, ERASEPROVISION; TEST ONLY");
  pinMode(PLANT_BOOT_PIN, INPUT_PULLUP);
  delay(1200);

  pinMode(PAR_EN,OUTPUT); digitalWrite(PAR_EN,LOW);
  parSerial.begin(4800,SERIAL_8N1,PAR_RX,PAR_TX);
  Wire.begin(SHT_SDA, SHT_SCL);
  Wire.setClock(100000);
  setenv("TZ","CST-8",1); tzset();
  rtcRestoreAtBoot();
  updateSHT30();
  runMicroSDTest();

  prefs.begin("wifi", false);
  wifiName = prefs.getString("ssid", "DF");
  wifiPass = prefs.getString("password", "");
  // V4.29: ignore legacy appass in NVS; do not overwrite or erase it.
  // Pairing identity is immutable across customer Wi-Fi changes and BOOT setup.

  otaPassword = prefs.getString("otapass", "");
  if (otaPassword.length() < 12) {
    otaPassword = makePassword();
    prefs.putString("otapass", otaPassword);
  }

  // Factory AP password is compiled per device; never regenerated at runtime.

  firebaseRestoreRefreshToken();
  bool connected = connectNetwork(wifiName, wifiPass);
  Serial.println(connected ?
    "WIFI CONNECT: PASS" : "WIFI CONNECT: FAIL");

  if (connected) {
    Serial.print("IP ADDRESS: ");
    Serial.println(WiFi.localIP());
  }

  setupRoutes();
  // ================================================================
  // VERSION-ONLY DIAGNOSTICS: REPLACE THIS BLOCK ON EVERY NEW RELEASE.
  // V4.29: verify factory pairing config without disclosing password.
  // Permanent diagnostics (/api/par-sync, /api/time-standard-verify,
  // /ota-status, etc.) are NOT part of this replaceable block.
  // ================================================================
  web.on("/api/release-diag", HTTP_GET, []() {
    String response = "{\"firmware\":\"" + String(FW_VERSION) +
      "\",\"test\":\"factory_pairing\",\"factory_ssid\":\"" +
      String(SETUP_SSID) + "\",\"setup_ip\":\"" +
      String(PLANT_FACTORY_SETUP_IP) + "\",\"password_configured\":" +
      String(strlen(setupPass) >= 8 && strlen(setupPass) <= 63 ? "true" : "false") +
      ",\"legacy_appass_ignored\":true}";
    web.send(200, "application/json; charset=utf-8", response);
  });
  // END VERSION-ONLY DIAGNOSTICS
  // Local-network OTA maintenance mode: activate before Arduino IDE upload.
  // Keeps sensor acquisition, SD logging and web server running; pauses
  // blocking Firebase/Arduino Cloud network calls for ten minutes.
  web.on("/ota-maintenance", HTTP_GET, []() {
    otaMaintenanceActive = true;
    otaMaintenanceStartedMs = millis();
    web.send(200, "text/html; charset=utf-8",
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'></head>"
      "<body><h2>OTA maintenance: READY</h2>"
      "<p>Firebase and Arduino Cloud network operations are paused for 10 minutes.</p>"
      "<p>Sensor sampling, MicroSD logging, local web and OTA remain active.</p>"
      "<p>Now upload from Arduino IDE to this device's OTA network port.</p>"
      "<p><a href='/ota-status'>OTA diagnostics</a></p></body></html>");
  });
  web.on("/ota-status", HTTP_GET, []() {
    String body = "<!doctype html><html><head><meta charset='utf-8'>"
                  "<meta http-equiv='refresh' content='5'></head><body><h2>OTA status</h2>";
    body += "<p>OTA enabled: " + String(otaReady ? "YES" : "NO") + "</p>";
    body += "<p>Cloud paused: " + String(otaCloudPaused() ? "YES" : "NO") + "</p>";
    body += "<p>Transfer active: " + String(otaTransferActive ? "YES" : "NO") + "</p>";
    body += "<p>Last loop duration (ms): " + String(otaLastLoopDurationMs) + "</p>";
    body += "<p>Max loop duration (ms): " + String(otaLongestLoopMs) + "</p>";
    body += "<p>Max cloud section duration (ms): " + String(otaLongestCloudMs) + "</p>";
    body += "<p><a href='/ota-maintenance'>Enable 10-minute OTA window</a></p></body></html>";
    web.send(200, "text/html; charset=utf-8", body);
  });
  // V4.23: local read-only history sync diagnostics; never display credentials.
  web.on("/api/par-read-probe", HTTP_GET, []() {
    if(!parSyncDate425.length() || !parSyncKey425.length()) {
      web.send(409,"application/json","{\"state\":\"NO_UPLOADED_KEY\"}");return;
    }
    parProbeRead425(parSyncDate425,parSyncKey425);
    web.send(200,"application/json; charset=utf-8",parSyncJson425());
  });
  web.on("/api/verify-sync", HTTP_GET, [](){
    web.send(200,"application/json; charset=utf-8",verifyBothOnce425());
  });
  web.on("/api/unified-sync", HTTP_GET, [](){web.send(200,"application/json; charset=utf-8",unifiedSyncJson425());});
  web.on("/api/time-standard-verify", HTTP_GET, [](){
    web.send(200,"application/json; charset=utf-8",verifyBothOnce425());
  });
  web.on("/api/history-audit", HTTP_GET, [](){
    web.sendHeader("Cache-Control","no-store");
    web.send(200,"application/json; charset=utf-8",auditReport426());
  });
  web.on("/history-audit", HTTP_GET, [](){
    if(!auditActive426)auditStart426();
    String page=R"HTML(<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>歷史資料完整性診斷</title><style>body{font-family:system-ui;background:#102019;color:#e9f4eb;padding:16px;line-height:1.5}main{max-width:700px;margin:auto}pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#203b2e;padding:14px;border-radius:12px;font-size:12px}button{font-size:17px;padding:12px;border:0;border-radius:10px;background:#80c79d;color:#102019}</style></head><body><main><h2>V4.26 歷史資料完整性診斷</h2><p>唯讀比較最近三天 00:00–08:59：MicroSD 與 Firebase 每小時筆數。自動逐時查詢，不修改原始資料或同步游標。</p><p id="status">讀取中...</p><button onclick="navigator.clipboard.writeText(document.getElementById('report').textContent).then(()=>alert('報告已複製'))">複製完整診斷報告</button><pre id="report">等待資料...</pre><script>async function refresh(){try{let r=await fetch('/api/history-audit?t='+Date.now());let j=await r.json();document.getElementById('status').textContent=j.state+' '+j.step+'/'+j.total_steps;document.getElementById('report').textContent=JSON.stringify(j,null,2);if(j.state!=='COMPLETE'&&j.state!=='CLOCK_INVALID')setTimeout(refresh,3500)}catch(e){document.getElementById('status').textContent='讀取失敗：'+e.message;setTimeout(refresh,5000)}}refresh();</script></main></body></html>)HTML";
    web.sendHeader("Cache-Control","no-store");web.send(200,"text/html; charset=utf-8",page);
  });

  web.on("/api/par-sync", HTTP_GET, []() {
    web.send(200,"application/json; charset=utf-8",parSyncJson425());
  });
  web.on("/par-sync", HTTP_GET, []() {
    String h="<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><meta http-equiv='refresh' content='15'></head><body style='font-family:sans-serif;padding:16px'>";
    h+="<h2>V4.25 PAR MicroSD + Firebase 同步診斷</h2><p>自動更新 15 秒；PASS 必須依真實 SD 與 HTTP 結果判斷。</p>";
    h+="<pre style='white-space:pre-wrap;overflow-wrap:anywhere'>"+parSyncJson425()+"</pre>";
    h+="<p><a href='/api/par-read-probe'>執行 Firebase PAR 讀回檢查</a> | <a href='/api/par-sync'>JSON 診斷</a> | <a href='/firebase-history'>既有歷史同步</a> | <a href='/par'>PAR</a></p></body></html>";
    web.send(200,"text/html; charset=utf-8",h);
  });
  web.on("/firebase-history", HTTP_GET, [](){
    web.sendHeader("Location","/api/unified-sync",true);web.send(302,"text/plain","");
  });
  sntp_set_time_sync_notification_cb(rtcNtpCallback);
  if (connected) { setenv("TZ","CST-8",1); tzset(); configTime(0, 0, "pool.ntp.org", "time.google.com"); setenv("TZ","CST-8",1); tzset(); }
  if (connected) startOTA();
  if (connected) beginPlantCloud(wifiName, wifiPass);
  Serial.print("OTA PASSWORD (KEEP PRIVATE): ");
  Serial.println(otaPassword);
  if (!connected) Serial.println("HOLD BOOT 5 SECONDS FOR SETUP");
}


void loop() {
  const unsigned long loopStartedMs = millis();
  // Always service OTA before potentially slow sensor/SD/web/network work.
  otaService();
  identityPollSerial();
  updateSHT30();
  rtcServiceNtp();
  d1SamplePAR();
  d1WriteLog();
  otaService();
  if (setupMode) setupDNS.processNextRequest();
  web.handleClient();
  otaService();
  // Advance at most one read-only Firebase audit query per loop.
  if(auditActive426 && !auditDone426 && !otaCloudPaused())auditAdvance426();
  updateNetworkDiagnostics();
  otaService();

  // During the explicit OTA window, skip synchronous Firebase HTTPS and
  // Arduino Cloud calls; local sensing and SD logging continue unchanged.
  if (!otaCloudPaused()) {
    const unsigned long cloudStartedMs = millis();
    updateFirebase();
    otaService();
    if (!otaCloudPaused()) updateUnifiedSync425();
    otaService();
    if (!otaCloudPaused() && cloudStarted && !setupMode && !changePending &&
        WiFi.status() == WL_CONNECTED) {
      ArduinoCloud.update();
    }
    const uint32_t cloudDuration = millis() - cloudStartedMs;
    if (cloudDuration > otaLongestCloudMs) otaLongestCloudMs = cloudDuration;
  }
  otaService();

  if (digitalRead(PLANT_BOOT_PIN) == LOW) {
    if (bootStarted == 0) bootStarted = millis();
    if (!setupMode && millis() - bootStarted >= 5000) {
      startSetup();
    }
  } else {
    if (bootStarted != 0) {
      unsigned long heldMs = millis() - bootStarted;
      // Release-only action, to avoid conflict with existing 5-second Wi-Fi setup.
      if (!setupMode && heldMs >= 3000 && heldMs < 5000) {
        identity_v418ProvisionLocally();
      }
    }
    bootStarted = 0;
  }

  if (changePending && millis() - changeAt >= 1000) {
    changePending = false;
    bool ok = connectNetwork(newName, newPass);
    if (ok) {
      prefs.putString("ssid", newName);
      prefs.putString("password", newPass);
      wifiName = newName;
      wifiPass = newPass;
      provisionState = "success";
      provisionStage = "dhcp_ready";
      provisionError = "";
      provisionIP = WiFi.localIP().toString();
      provisionSuccessAt = millis();
      Serial.println("NEW WIFI: PASS");
      Serial.print("IP ADDRESS: ");
      Serial.println(WiFi.localIP());
      // Keep setup AP alive briefly so the phone can read the actual DHCP IP.
      configTime(0, 0, "pool.ntp.org", "time.google.com"); setenv("TZ","CST-8",1); tzset();
      firebaseIdToken = "";
      // Refresh token remains valid across Wi-Fi changes.
      firebaseRetryWaitMs = FIREBASE_RETRY_INTERVAL_MS;
      firebaseLoginAttempted = false;
      startOTA();
      if (cloudStarted) {
        cloudStarted = false;
        Serial.println("CLOUD: Wi-Fi changed; restart ESP32 to resume Cloud");
      } else {
        beginPlantCloud(wifiName, wifiPass);
      }
    } else {
      Serial.println("NEW WIFI: FAIL");
      provisionState = "failed";
      provisionStage = "sta_connect";
      provisionError = "E-03";
      provisionIP = "";
      // Preserve AP while restoring the last known working STA connection.
      connectNetwork(wifiName, wifiPass);
      touchSetup();
    }
    newName = "";
    newPass = "";
  }

  if (setupMode && provisionState == "success" &&
      millis() - provisionSuccessAt >= SETUP_SUCCESS_GRACE_MS) {
    stopSetup();
  } else if (setupMode && provisionState != "success" &&
             !changePending && millis() - setupStarted > SETUP_LIMIT) {
    stopSetup();
  }
  delay(10);
  otaLastLoopDurationMs = millis() - loopStartedMs;
  if (otaLastLoopDurationMs > otaLongestLoopMs) otaLongestLoopMs = otaLastLoopDurationMs;
}