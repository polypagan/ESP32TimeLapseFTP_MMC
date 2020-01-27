/*
 * 20200122
 * 
 * DdelV downloaded from: https://robotzero.one/time-lapse-esp32-cameras/
 * and massaged.
 * 
 * Use Serial.printf() wherever possible
 * Add smartconfig (replace baked-in credentials.)
 * 
 * Can something be done about that pesky flashLED?
 * 
 * Merged with robo8080 FTP_SD code
 * failed to get that working.
 * 
 * So, tried code from 
 * https://github.com/mtnbkr88/ESP32_FTPServer.git
 * which works!
 * (one difference is in init_card(), where max_files is set to 10.)
 * 
 * Added Arduino and web OTA
 * (modified boards.txt to allow selecting partition size.)
 * 
 * DdelV 202200125
 * 
 * ToDo: 
 *  add SmartConfig. Done 20200127 (not tested)
 *  remove all dynamic memory referenced ("String")
 *  add globbing to FTP server.
 * 
 */

#include "esp_camera.h"
#include <WiFi.h>
// FTP server
#include <WiFiClient.h>
#include "ESP32FtpServer.h"
// Do we really need this? Test.
#include "Arduino.h"
// Time
#include "time.h"
#include "lwip/err.h"
#include "lwip/apps/sntp.h"
// MicroSD
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
// for OTA
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
// web updates
#include <WebServer.h>
#include <Update.h>
// other includes from mtnbkr88 code:
#include "esp_deep_sleep.h"
#include "time.h"

// Edit capture_interval:
int capture_interval = 5000; // microseconds between captures

#define FTP_DEBUG true

FtpServer ftpSrv;   //set #define FTP_DEBUG in ESP32FtpServer.h to see ftp verbose on serial

long current_millis;
long last_capture_millis = 0;
static esp_err_t cam_err;
static esp_err_t card_err;
char strftime_buf[64];
int file_number = 0;
bool internet_connected = false;
struct tm timeinfo;
time_t now;

const char *host = "ESP32-CAM0";
WebServer server(80);
#include "updateWeb.h"

// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASHLIGHT 4
#define LEDPIN 33
#define LEDON LOW
#define LEDOFF HIGH

void setup() {
  Serial.begin(115200);

  Serial.setDebugOutput(true);

  Serial.println("Esp32CamTimelapseFTP_MMC.ino!");

  pinMode(LEDPIN, OUTPUT);        // little red led on back of chip
  digitalWrite(LEDPIN, LEDON);      // turn on the red LED on the back of chip
  
  if (init_wifi()) { // Connected to WiFi
    internet_connected = true;
    Serial.println("Internet connected");
    init_time();
    time(&now);
    setenv("TZ", "EST5EDT4,M3.2.0/02:00:00,M11.1.0/02:00:00", 1);
    tzset();
  }

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  //init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // camera init
  cam_err = esp_camera_init(&config);
  if (cam_err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", cam_err);
    return;
  }
  // SD card init
  card_err = init_sdcard();
  if (card_err != ESP_OK) {
    Serial.printf("SD Card init failed with error 0x%x", card_err);
    return;
  }

  ftpSrv.begin("esp32","esp32");

  digitalWrite(LEDPIN, LEDOFF);     // turn off the red LED on the back of the chip

  Serial.print("FTP Server Ready! Use IP '");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");  
}

void SmartConfig() {
  Serial.println("Starting SmartConfig");
  WiFi.beginSmartConfig();
    while(1){
    #if defined(LEDPIN)
      digitalWrite(LEDPIN, LEDON);
    #endif
      delay(500);
    #if defined(LEDPIN)
      digitalWrite(LEDPIN, LEDOFF);
    #endif
      delay(500);
      if(WiFi.smartConfigDone()){
        Serial.println("SmartConfig Success");
        break;
      }
    }
    WiFi.stopSmartConfig();
}

bool init_wifi()
{
  int connAttempts = 0;

  WiFi.mode(WIFI_STA);
  
  Serial.println("\r\nConnecting to WiFi network.");
  WiFi.begin();
  for (connAttempts = 0; WiFi.status() != WL_CONNECTED; connAttempts++ ) {
    if (connAttempts >= 20) {  // timed out
      SmartConfig();
      connAttempts = 0;
    }    
    delay(400);
    Serial.print(".");
  }

// interpose OTA boilerplate:
  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname(host);

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";
   
      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

// end OTA boilerplate.
// web OTA boilerplate
  /*use mdns for host name resolution*/
  if (!MDNS.begin(host)) { //http://esp32.local
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  /*return index page which is stored in serverIndex */
  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
     
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
}
  });
  server.begin();
// end web OTA boilerplate    
  return true;
}

void init_time()
{
  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  sntp_setservername(0, "192.168.254.245");   // was "pool.ntp.org");
  sntp_init();
  // wait for time to be set
  time_t now = 0;
  timeinfo = { 0 };
  int retry = 0;
  const int retry_count = 10;
  while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count) {
    Serial.printf("Waiting for system time to be set... (%d/%d)\n", retry, retry_count);
    delay(2000);
    time(&now);
    localtime_r(&now, &timeinfo);
  }
}

static esp_err_t init_sdcard()
{
  esp_err_t ret = ESP_FAIL;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 10,
  };
  sdmmc_card_t *card;

  Serial.println("Mounting SD card...");
  ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

  if (ret == ESP_OK) {
    Serial.println("SD card mount successful!");
  }  else  {
    Serial.printf("Failed to mount SD card VFAT filesystem. Error: %s", esp_err_to_name(ret));
  }
  Serial.print("SD_MMC Begin: "); Serial.println(SD_MMC.begin());
  
}

static esp_err_t save_photo_numbered()
{
  file_number++;
  Serial.printf("Taking picture: %d", file_number);
  
  camera_fb_t *fb = esp_camera_fb_get();

  //char *filename = (char*)malloc(21 + sizeof(int));
  char *filename = (char*)malloc(21 + sizeof(file_number));
  sprintf(filename, "/sdcard/capture_%d.jpg", file_number);

  Serial.println(filename);
  FILE *file = fopen(filename, "w");
  if (file != NULL)  {
    size_t err = fwrite(fb->buf, 1, fb->len, file);
    Serial.printf("File saved: %s\n", filename);
  }  else  {
    Serial.println("Could not open file");
  }
  fclose(file);
  esp_camera_fb_return(fb);
  free(filename);
}

static esp_err_t save_photo_dated()
{
  Serial.println("Taking picture...");
  camera_fb_t *fb = esp_camera_fb_get();

  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%F_%H_%M_%S", &timeinfo);

  char *filename = (char*)malloc(21 + sizeof(strftime_buf));
  sprintf(filename, "/sdcard/capture_%s.jpg", strftime_buf);

  Serial.println(filename);
  FILE *file = fopen(filename, "w");
  if (file != NULL)  {
    size_t err = fwrite(fb->buf, 1, fb->len, file);
    Serial.printf("File saved: %s\n", filename);

  }  else  {
    Serial.println("Could not open file");
  }
  fclose(file);
  esp_camera_fb_return(fb);
  free(filename);
}

void save_photo()
{
  if (timeinfo.tm_year < (2016 - 1900) || internet_connected == false) { // if no internet or time not set
    save_photo_numbered(); // filenames in numbered order
  } else {
    save_photo_dated(); // filenames with date and time
  }
}

void loop()
{
//  current_millis = millis();
  do {
    ftpSrv.handleFTP();
    server.handleClient();
    ArduinoOTA.handle();    
  } while (millis() - last_capture_millis < capture_interval);

// Take another picture on your way out.  
    last_capture_millis = millis();
    save_photo();
}
