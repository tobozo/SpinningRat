/// need ESP8266Audio library. ( URL : https://github.com/earlephilhower/ESP8266Audio/ )
#include <AudioOutput.h>
#include <AudioFileSourcePROGMEM.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>
// need tgx 3D library
#include <tgx.h> // https://github.com/vindar/tgx
// need M5Unified
#include <M5Unified.h>
#define tft M5.Lcd

#define USE_SDUPDATER

#define SDU_APP_NAME   "SpinningRat" // app title for the sd-updater lobby screen
#define SDU_APP_PATH   "/SpinningRat.bin"     // app binary file name on the SD Card (also displayed on the sd-updater lobby screen)
#define SDU_APP_AUTHOR "@tobozo"           // app binary author name for the sd-updater lobby screen
#if defined USE_SDUPDATER
  #include <M5StackUpdater.h>
#endif

// 3D and audio assets
#include "./audio.h"
#include "./free_bird.h"
#include "rat.h"

/// set M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;
static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
static AudioGeneratorMP3 *mp3        = nullptr; //new AudioGeneratorMP3;
static AudioFileSourceID3 *id3       = nullptr; //new AudioFileSourceID3( pgmem );
static AudioFileSourcePROGMEM *pgmem = nullptr; // new AudioFileSourcePROGMEM( free_bird_mp3, free_bird_mp3_len );

static int loopnumber = 0;
static int prev_loopnumber = -1;

// size of the drawing framebuffer
// (limited by the amount of memory in the ESP32).
#define SLX 320
#define SLY 140

// the framebuffer we draw onto
static uint16_t fb[SLX * SLY];
// the z-buffer in 16 bits precision
static uint16_t* zbuf;

using namespace tgx;

// the image that encapsulate framebuffer fb
static Image<RGB565> imfb(fb, SLX, SLY);

// only load the shaders we need.
static const int LOADED_SHADERS = TGX_SHADER_PERSPECTIVE | TGX_SHADER_ZBUFFER | TGX_SHADER_FLAT | TGX_SHADER_GOURAUD | TGX_SHADER_NOTEXTURE | TGX_SHADER_TEXTURE_NEAREST |TGX_SHADER_TEXTURE_WRAP_POW2;

// the renderer object that performs the 3D drawings
static Renderer3D<RGB565, LOADED_SHADERS, uint16_t> renderer;


/** Compute the model matrix according to the current time */
tgx::fMat4 moveModel(int& loopnumber)
{
  const float end1 = 6000;
  const float end2 = 2000;
  const float end3 = 6000;
  const float end4 = 2000;

  int tot = (int)(end1 + end2 + end3 + end4);
  int m = millis();

  loopnumber = m / tot;
  float t = m % tot;

  const float dilat = 16; // scale model
  const float roty = 360 * (t / 4000); // rotate 1 turn every 4 seconds
  float tz=-25, ty=0;
  if (t < end1) { // far away
    tz = -25;
    ty = 0;
  } else {
    t -= end1;
    // if (t < end2) { // zooming in
    //   t /= end2;
    //   tz = -25 + 18 * t;
    //   ty = -6.5f * t;
    // } else {
    //   t -= end2;
    //   if (t < end3) { // close up
    //     tz = -7;
    //     ty = -6.5f;
    //   } else { // zooming out
    //     t -= end3;
    //     t /= end4;
    //     tz = -7 - 18 * t;
    //     ty = -6.5 + 6.5 * t;
    //   }
    // }
  }

  fMat4 M;
  M.setScale({ dilat, dilat, dilat }); // scale the model
  M.multRotate(-roty, { 0,1,0 }); // rotate around y
  M.multTranslate({ 0,ty, tz }); // translate
  return M;
}


static void loop_mp3_task( void * param )
{
  goto _start;

  while(1) {
    if (mp3 && mp3->isRunning()) {
      if (!mp3->loop()) {
        mp3->stop();
        goto _start;
      }
    } else {
      _start:
      if( id3) delete id3;
      if( mp3) delete mp3;
      if( pgmem) delete pgmem;
      pgmem = new AudioFileSourcePROGMEM( free_bird_mp3, free_bird_mp3_len );
      id3 = new AudioFileSourceID3( pgmem );
      mp3 = new AudioGeneratorMP3;
      mp3->begin(id3, &out);
      vTaskDelay(10);
    }
    vTaskDelay(1);
  }
}



static void loop_3d_task( void * param )
{
  // allocate the zbuffer
  zbuf = (uint16_t*)malloc(SLX * SLY * sizeof(uint16_t));
  while (zbuf == nullptr) {
    Serial.println("Error: cannot allocate memory for zbuf");
    delay(1000);
  }

  // setup the 3D renderer.
  renderer.setViewportSize(SLX,SLY);
  renderer.setOffset(0, 0);
  renderer.setImage(&imfb); // set the image to draw onto (ie the screen framebuffer)
  renderer.setZbuffer(zbuf); // set the z buffer for depth testing
  renderer.setPerspective(45, ((float)SLX) / SLY, 1.0f, 100.0f);  // set the perspective projection matrix.
  renderer.setMaterial(RGBf(0.85f, 0.55f, 0.25f), 0.2f, 0.7f, 0.8f, 64); // bronze color with a lot of specular reflexion.
  renderer.setCulling(1);
  renderer.setTextureQuality(TGX_SHADER_TEXTURE_NEAREST);
  renderer.setTextureWrappingMode(TGX_SHADER_TEXTURE_WRAP_POW2);

  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED);
  tft.startWrite();

  while(1) {
    // compute the model position
    fMat4  M = moveModel(loopnumber);
    renderer.setModelMatrix(M);

    // draw the 3D mesh
    imfb.fillScreen(RGB565_Black);              // clear the framebuffer (black background)
    renderer.clearZbuffer();                    // clear the z-buffer

    // choose the shader to use
    switch (loopnumber % 4) {
      case 0: renderer.setShaders(TGX_SHADER_GOURAUD | TGX_SHADER_TEXTURE); renderer.drawMesh(&rat, false); break;
      case 1: renderer.drawWireFrameMesh(&rat, true); break;
      case 2: renderer.setShaders(TGX_SHADER_FLAT); renderer.drawMesh(&rat, false); break;
      case 3: renderer.setShaders(TGX_SHADER_GOURAUD); renderer.drawMesh(&rat, false); break;
    }

    if (prev_loopnumber != loopnumber) {
      prev_loopnumber = loopnumber;
      tft.fillRect(0, 300, 240, 20, TFT_BLACK);
      tft.setCursor(5, 305);
      switch (loopnumber % 4) {
        case 0: tft.print("Gouraud shading / texturing"); break;
        case 1: tft.print("Wireframe"); break;
        case 2: tft.print("Flat Shading"); break;
        case 3: tft.print("Gouraud shading"); break;
      }
    }

    tft.waitDMA();
    tft.pushImageDMA((tft.width() - SLX) / 2, (tft.height() - SLY) / 2, SLX, SLY, fb/*, fb2*/);
  }
}


void setup()
{
  M5.begin();
  M5.Speaker.begin();

  #if defined USE_SDUPDATER
    SDUCfg.setAppName(SDU_APP_NAME);      // lobby screen label: application name
    SDUCfg.setBinFileName(SDU_APP_PATH);  // if file path to bin is set for this app, it will be checked at boot and created if not exist

    #if M5_SD_UPDATER_VERSION_INT >= VERSION_VAL(1, 2, 8)
    // New SD Updater support, requires version >=1.2.8 of https://github.com/tobozo/M5Stack-SD-Updater/
    if( Flash::hasFactoryApp() ) {
      SDUCfg.setLabelMenu("FW Menu");
      SDUCfg.setLabelRollback("Save FW");
      SDUCfg.rollBackToFactory = true;
      checkFWUpdater( 5000 );
    } else
    #endif
    {
      SDUCfg.setLabelMenu("SD Menu");
      checkSDUpdater( &SD, "", 5000, TFCARD_CS_PIN );
    }
  #endif

  xTaskCreatePinnedToCore( loop_mp3_task, "MP3", 4096, nullptr, 8, NULL, 0 );
  xTaskCreatePinnedToCore( loop_3d_task, "3D", 8192, nullptr, 8, NULL, 1 );
}


void loop()
{
  vTaskDelete(NULL);
}



