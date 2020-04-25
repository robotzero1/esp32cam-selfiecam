#include <ESPAsyncWebServer.h>
#include "esp_camera.h"
#include "camera_index.h"
#include "Arduino.h"
#include "fd_forward.h"


#include <SPI.h>
#include <TFT_eSPI.h>
TFT_eSPI tft = TFT_eSPI();
#include <TFT_eFEX.h>
TFT_eFEX  fex = TFT_eFEX(&tft);

AsyncWebServer webserver(80);
AsyncWebSocket ws("/ws");

const char* ssid = "NSA HONEYPOT";
const char* password = "only4andrew";

String filelist;
camera_fb_t * fb = NULL;
String incoming;
long current_millis;
long last_capture_millis = 0;
static esp_err_t cam_err;
static esp_err_t card_err;
char strftime_buf[64];
int file_number = 0;


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

static inline mtmn_config_t app_mtmn_config()
{
  mtmn_config_t mtmn_config = {0};
  mtmn_config.type = FAST;
  mtmn_config.min_face = 60; // 80 default
  mtmn_config.pyramid = 0.707;
  mtmn_config.pyramid_times = 4;
  mtmn_config.p_threshold.score = 0.6;
  mtmn_config.p_threshold.nms = 0.7;
  mtmn_config.p_threshold.candidate_number = 20;
  mtmn_config.r_threshold.score = 0.7;
  mtmn_config.r_threshold.nms = 0.7;
  mtmn_config.r_threshold.candidate_number = 10;
  mtmn_config.o_threshold.score = 0.7;
  mtmn_config.o_threshold.nms = 0.7;
  mtmn_config.o_threshold.candidate_number = 1;
  return mtmn_config;
}
mtmn_config_t mtmn_config = app_mtmn_config();

void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{
  // String incoming = String((char *)data); No idea why.. gave extra characters in data for short names.
  // so ....
  for (size_t i = 0; i < len; i++) {
    incoming += (char)(data[i]);
  }
  Serial.println(incoming);

  if (incoming.substring(0, 7) == "delete:") {
    String deletefile = incoming.substring(7);
    incoming = "";
    Serial.println(deletefile);
    Serial.println("image delete");
    if (SPIFFS.remove("/" + deletefile)) {
      client->text("removed:" + deletefile); // file deleted. request browser update
    }
  } else {
    Serial.print("sending list");
    client->text(filelist_spiffs());
  }
}

String filelist_spiffs()
{
  fs::File root = SPIFFS.open("/");

  fs::File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    // Serial.println(fileName);
    filelist = filelist + fileName;
    file = root.openNextFile();
  }
  Serial.println(filelist);
  return filelist;
}

void latestFileSPIFFS()
{
  fs::File root = SPIFFS.open("/");

  fs::File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    Serial.println(fileName);
    int fromUnderscore = fileName.lastIndexOf('_') + 1;
    int untilDot = fileName.lastIndexOf('.');
    String fileId = fileName.substring(fromUnderscore, untilDot);
    Serial.print(fileId);
    file = root.openNextFile();
  }

}

void setup() {
  Serial.begin(115200);

  init_wifi();

  tft.begin();

  tft.setRotation(0);  // 0 & 2 Portrait. 1 & 3 landscape
  tft.fillScreen(TFT_BLACK);

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


  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QQVGA);

  ws.onEvent(onEvent);
  webserver.addHandler(&ws);

  webserver.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.print("wtf here");
    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", index_ov2640_html_gz, sizeof(index_ov2640_html_gz));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  webserver.on("/image", HTTP_GET, [](AsyncWebServerRequest * request) {
    Serial.println("requesting image from SPIFFS");
    if (request->hasParam("id")) {
      AsyncWebParameter* p = request->getParam("id");
      Serial.printf("GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      String imagefile = p->value();
      imagefile = imagefile.substring(4); // remove _img
      request->send(SPIFFS, "/" + imagefile);
    }
  });

  webserver.begin();

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialisation failed!");
    SPIFFS.begin(true);// Formats SPIFFS - could lose data https://github.com/espressif/arduino-esp32/issues/638
  }
  Serial.println("\r\nInitialisation done.");

  fex.listSPIFFS(); // Lists the files so you can see what is in the SPIFFS
}

bool init_wifi()
{
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.print("Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
  return true;
}

bool face_detected()
{
  fb = esp_camera_fb_get();
  dl_matrix3du_t *image_matrix = NULL;
  image_matrix = dl_matrix3du_alloc(1, fb->width, fb->height, 3);
  fmt2rgb888(fb->buf, fb->len, fb->format, image_matrix->item);
  box_array_t *net_boxes = NULL;
  net_boxes = face_detect(image_matrix, &mtmn_config);

  if (net_boxes) {
    free(net_boxes->score);
    free(net_boxes->box);
    free(net_boxes->landmark);
    free(net_boxes);
    dl_matrix3du_free(image_matrix);
    Serial.println("face  detected");
    esp_camera_fb_return(fb);
    fb = NULL;
    return true;
  } else {
    dl_matrix3du_free(image_matrix);

    esp_camera_fb_return(fb);
    fb = NULL;
    return false;
  }
}

void smile_for_the_camera()
{
  long timer_millis = millis();
  //while (millis() - timer_millis < 3000) {

  //  if (millis() - timer_millis < 1000) {
  fex.drawJpgFile(SPIFFS, "/_count3.jpg", 0, 0);
  delay(400); //smooth display of numbers
  //  }
  //  if (millis() - timer_millis > 1000 && millis() - timer_millis <= 2000) {
  fex.drawJpgFile(SPIFFS, "/_count2.jpg", 0, 0);
  delay(400); //smooth display of numbers
  //  }
  //  else if (millis() - timer_millis > 2000) {
  fex.drawJpgFile(SPIFFS, "/_count1.jpg", 0, 0);
  delay(400); //smooth display of numbers
  //  }

  //if (!face_detected()) { // if face disappears stop
  //  Serial.println("face not detected");
  //  return;
  //}
  //}
  take_photo(); //face still in frame (detected) for 2 seconds
}

static esp_err_t take_photo()
{
  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_SVGA);
  delay(500);
  fb = esp_camera_fb_get();
  fex.drawJpg((const uint8_t*)fb->buf, fb->len, 0, 0);
  latestFileSPIFFS();
  file_number++;
  Serial.print("Taking picture: ");
  //Serial.print(file_number);

  char *filename = (char*)malloc(21 + sizeof(file_number));
  sprintf(filename, "/spiffs/selfie_%d.jpg", file_number);
  Serial.println("Opening file: ");
  FILE *file = fopen(filename, "w");
  if (file != NULL)  {
    size_t err = fwrite(fb->buf, 1, fb->len, file);
    Serial.printf("File saved: %s\n", filename);
  }  else  {
    Serial.println("Could not open file");
  }
  fclose(file);
  Serial.println("return  : ");
  esp_camera_fb_return(fb);
  Serial.println("NULL  : ");
  fb = NULL;
  Serial.println("free  : ");
  free(filename);
  Serial.println("set_framesize  : ");
  s->set_framesize(s, FRAMESIZE_QQVGA);
  delay(500);
  char *addtofilename = (char*)malloc(22 + sizeof(file_number));
  sprintf(addtofilename, "added:selfie_%d.jpg", file_number);
  ws.textAll((char*)addtofilename);// file added. request browser update
}


void loop()
{

  if (face_detected()) {
    smile_for_the_camera();
  }
  else
  {
    fb = esp_camera_fb_get();
    fex.drawJpg((const uint8_t*)fb->buf, fb->len, 0, 0);
    esp_camera_fb_return(fb);
    fb = NULL;
  }

}
