#pragma once

#include "wled.h"

//FOR TFT Settings COPY your OWN "User_Setup.h"

#include <FS.h>
#include <SD.h>
#include <JPEGDecoder.h>
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <HardwareSerial.h>



//Smooth Fonts
#include "valorax64.h"
#include "Akashi32.h"
#include "Joystix16.h"


/*
 * Usermods allow you to add own functionality to WLED more easily
 * See: https://github.com/Aircoookie/WLED/wiki/Add-own-functionality
 * 
 * This is an example for a v2 usermod.
 * v2 usermods are class inheritance based and can (but don't have to) implement more functions, each of them is shown in this example.
 * Multiple v2 usermods can be added to one compilation easily.
 * 
 * Creating a usermod:
 * This file serves as an example. If you want to create a usermod, it is recommended to use usermod_v2_empty.h from the usermods folder as a template.
 * Please remember to rename the class and file to a descriptive name.
 * You may also use multiple .h and .cpp files.
 * 
 * Using a usermod:
 * 1. Copy the usermod into the sketch folder (same folder as wled00.ino)
 * 2. Register the usermod by adding #include "usermod_filename.h" in the top and registerUsermod(new MyUsermodClass()) in the bottom of usermods_list.cpp
 */
#define USERMOD_ID_CPC       69

#define SD_CS    37  // Chip select SD Card

#define BUILDTM_YEAR (\
    __DATE__[7] == '?' ? 1900 \
    : (((__DATE__[7] - '0') * 1000 ) \
    + (__DATE__[8] - '0') * 100 \
    + (__DATE__[9] - '0') * 10 \
    + __DATE__[10] - '0'))

#define BUILDTM_MONTH (\
    __DATE__ [2] == '?' ? 1 \
    : __DATE__ [2] == 'n' ? (__DATE__ [1] == 'a' ? 1 : 6) \
    : __DATE__ [2] == 'b' ? 2 \
    : __DATE__ [2] == 'r' ? (__DATE__ [0] == 'M' ? 3 : 4) \
    : __DATE__ [2] == 'y' ? 5 \
    : __DATE__ [2] == 'l' ? 7 \
    : __DATE__ [2] == 'g' ? 8 \
    : __DATE__ [2] == 'p' ? 9 \
    : __DATE__ [2] == 't' ? 10 \
    : __DATE__ [2] == 'v' ? 11 \
    : 12)

#define BUILDTM_DAY (\
    __DATE__[4] == '?' ? 1 \
    : ((__DATE__[4] == ' ' ? 0 : \
    ((__DATE__[4] - '0') * 10)) + __DATE__[5] - '0'))


//class name. Use something descriptive and leave the ": public Usermod" part :)
class CPC : public Usermod {

  private:
    // Private class members. You can declare variables and functions only accessible to your usermod here
    bool enabled = false;
    bool initDone = false;

    TFT_eSPI tft = TFT_eSPI();
    bool sdCardEnabled = false;

    int posXHourMiddle=0;
    
    int posXHourRight=0;
    int posXHourLeft=0;
    uint8_t middleCar=0;

    int posYDate=24;
    int posYHour=96;
    int posYBME=208;
    
    char minutesString[4];
    char BMEString [20];
    char BMEStringHum [20];
    byte currentBri;

    // FROM https://chrishewett.com/blog/true-rgb565-colour-picker/ use RGB565 

    String dateColor = "0x0320";
    String BMEColor = "0x3231";
    String timeColor = "0xA800";

    //If we don't have any informations from the current time
    //We retrieve the build time and after we will increment it.
    //Not the best way, so please enable NTP in WLED
    //static uint8_t conv2d(const char* p); // Forward declaration needed for IDE 1.6.x
    uint8_t hh = conv2d(__TIME__), mm = conv2d(__TIME__ + 3), ss = conv2d(__TIME__ + 6); // Get H, M, S from compile time

    //Internal variables to detect movement
    byte omm = 99, oss = 99, ohh=99;

    //If the ESP has enough memory we will use to have a transpancy effet when we update the "minute"
    uint16_t* screenContent = NULL;
    //To avoid refresh too frequently
    uint32_t targetTime = 0;                    // for next 1 second timeout
    
    //If you have a BME280 and enabled the usermod we will show temperature & humidity
    uint8_t humidity = 0;
    int temperature = 0;
    //Sometimes we need calibration
    int temperatureCorrection = 0;
    uint8_t humidityCorrection = 0;

    // The library defines the type "setup_t" as a struct
    setup_t user;
    
    //Check if the user mode BME280 is enabled and get a pointer to the instance
    #ifdef USERMOD_BME280
       UsermodBME280* BME;
    #endif

    // string that are used multiple time (this will save some flash memory)
    static const char _name[];
    static const char _enabled[];

    // any private methods should go here (non-inline method should be defined out of class)
    void publishMqtt(const char* state, bool retain = false); // example for publishing MQTT message
    
    // Function to extract numbers from compile time string
    static uint8_t conv2d(const char* p) {
      uint8_t v = 0;
      if ('0' <= *p && *p <= '9')
        v = *p - '0';
      return 10 * v + *++p - '0';
    }

    // LIBS
    //####################################################################################################
    // Draw a JPEG on the TFT, images will be cropped on the right/bottom sides if they do not fit
    //####################################################################################################
    // This function assumes xpos,ypos is a valid screen coordinate. For convenience images that do not
    // fit totally on the screen are cropped to the nearest MCU size and may leave right/bottom borders.
    void jpegRender(int xpos, int ypos) {

      //jpegInfo(); // Print information from the JPEG file (could comment this line out)

      uint16_t *pImg;
      uint16_t mcu_w = JpegDec.MCUWidth;
      uint16_t mcu_h = JpegDec.MCUHeight;
      uint32_t max_x = JpegDec.width;
      uint32_t max_y = JpegDec.height;

      bool swapBytes = tft.getSwapBytes();
      // I don't kwow why but on my ESP8266 I need to remove, however in my ESP32 it works so...
      #if defined(ESP32)
        tft.setSwapBytes(true);
      #endif
      
      // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
      // Typically these MCUs are 16x16 pixel blocks
      // Determine the width and height of the right and bottom edge image blocks
      uint32_t min_w = jpg_min(mcu_w, max_x % mcu_w);
      uint32_t min_h = jpg_min(mcu_h, max_y % mcu_h);

      // save the current image block size
      uint32_t win_w = mcu_w;
      uint32_t win_h = mcu_h;

      // record the current time so we can measure how long it takes to draw an image
      //uint32_t drawTime = millis();

      // save the coordinate of the right and bottom edges to assist image cropping
      // to the screen size
      max_x += xpos;
      max_y += ypos;

      // Fetch data from the file, decode and display
      while (JpegDec.read()) {    // While there is more data in the file
        pImg = JpegDec.pImage ;   // Decode a MCU (Minimum Coding Unit, typically a 8x8 or 16x16 pixel block)

        // Calculate coordinates of top left corner of current MCU
        uint32_t mcu_x = JpegDec.MCUx * mcu_w + xpos;
        uint32_t mcu_y = JpegDec.MCUy * mcu_h + ypos;

        // check if the image block size needs to be changed for the right edge
        if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
        else win_w = min_w;

        // check if the image block size needs to be changed for the bottom edge
        if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
        else win_h = min_h;

        // copy pixels into a contiguous block
        if (win_w != mcu_w)
        {
          uint16_t *cImg;
          int p = 0;
          cImg = pImg + win_w;
          for (uint32_t h = 1; h < win_h; h++)
          {
            p += mcu_w;
            for (uint32_t w = 0; w < win_w; w++)
            {
              *cImg = *(pImg + w + p);
              cImg++;
            }
          }
        }

        // calculate how many pixels must be drawn
        //uint32_t mcu_pixels = win_w * win_h;

        // draw image MCU block only if it will fit on the screen
        if (( mcu_x + win_w ) <= (uint32_t)tft.width() && ( mcu_y + win_h ) <= (uint32_t)tft.height())
          tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
        else if ( (mcu_y + win_h) >= (uint32_t)tft.height())
          JpegDec.abort(); // Image has run off bottom of screen so abort decoding
      }

      tft.setSwapBytes(swapBytes);
    }

    //####################################################################################################
    // Print image information to the serial port (optional)
    //####################################################################################################
    // JpegDec.decodeFile(...) or JpegDec.decodeArray(...) must be called before this info is available!
    void jpegInfo() {
      // Print information extracted from the JPEG file
      Serial.println("JPEG image info");
      Serial.println("===============");
      Serial.print("Width      :");
      Serial.println(JpegDec.width);
      Serial.print("Height     :");
      Serial.println(JpegDec.height);
      Serial.print("Components :");
      Serial.println(JpegDec.comps);
      Serial.print("MCU / row  :");
      Serial.println(JpegDec.MCUSPerRow);
      Serial.print("MCU / col  :");
      Serial.println(JpegDec.MCUSPerCol);
      Serial.print("Scan type  :");
      Serial.println(JpegDec.scanType);
      Serial.print("MCU width  :");
      Serial.println(JpegDec.MCUWidth);
      Serial.print("MCU height :");
      Serial.println(JpegDec.MCUHeight);
      Serial.println("===============");
      Serial.println("");
    }

    //####################################################################################################
    // Draw a JPEG on the TFT pulled from SD Card
    //####################################################################################################
    // xpos, ypos is top left corner of plotted image
    void drawSdJpeg(const char *filename, int xpos, int ypos) {
      // Open the named file (the Jpeg decoder library will close it)
      File jpegFile = SD.open( filename, FILE_READ);  // or, file handle reference for SD library
    
      if ( !jpegFile ) {
        Serial.print("ERROR: File \""); Serial.print(filename); Serial.println ("\" not found!");
        return;
      }

      Serial.println("===========================");
      Serial.print("Drawing file: "); Serial.println(filename);
      Serial.println("===========================");

      // Use one of the following methods to initialise the decoder:
      bool decoded = JpegDec.decodeSdFile(jpegFile);  // Pass the SD file handle to the decoder,
      //bool decoded = JpegDec.decodeSdFile(filename);  // or pass the filename (String or character array)

      if (decoded) {
        // print information about the image to the serial port
        jpegInfo();
        // render the image onto the screen at given coordinates
        jpegRender(xpos, ypos);
      }
      else {
        Serial.println("Jpeg file format not supported!");
      }
    }    
    //####################
    // Show Date
    // If you don't have NTP enabled, you will see the same date of the build forever :)
    //####################
    void showDate(uint8_t dateypos) {
      tft.loadFont(Akashi32);
      //tft.setTextColor(TFT_DARK_GREEN, TFT_BLACK);
      tft.setTextColor((uint16_t)strtol(dateColor.c_str(), NULL, 0), TFT_BLACK);
      tft.setTextDatum(MC_DATUM);//Set the middle for the coordinate
      char curDate[30];
      if(year(localTime)!=1970)
        sprintf(curDate,"%02d/%02d/%02d",(int)day(localTime),(int)month(localTime),(int)year(localTime));
      else
        sprintf(curDate,"%02d/%02d/%02d",BUILDTM_DAY,BUILDTM_MONTH,BUILDTM_YEAR);
      //Serial.printf("Date %s\n",curDate);
      tft.drawString(curDate, tft.width() / 2, dateypos);
      tft.unloadFont();
    }

    //####################
    // showHour
    // foreach hour we load a new background
    //####################
    void showHour(byte myhour) {
      if(sdCardEnabled) {
        char backgroundFilename[32];
        sprintf(backgroundFilename,"/background-%02d.jpg",myhour);
        drawSdJpeg(backgroundFilename, 0, 0);
      }
      // New Hour so we need to show date after the loading
      showDate(posYDate);

      tft.setTextDatum(TL_DATUM);//All Coordinates "Top left" !!!   

      //Depending of your memory, you will have a rounded black for minutes or a transparent effet 
      //The memory on ESP8266 is generally low and on ESP32 we can use PSRAM (please check your ESP specification)      
      Serial.printf("Memory Needed %d\n", (112*64*sizeof(uint16_t)) );
      free(screenContent);
      #if defined (ESP8266)        
        Serial.printf("Memory Free %d\n",ESP.getFreeHeap());
        //screenContent = ( uint16_t*) calloc(112*64, sizeof(uint16_t)); //Need a lot of Memory, so use if you enougth (like X2)
      #endif
      #if defined (ESP32)
        Serial.printf("PS Ram Free %d\n",ESP.getFreePsram());
        screenContent = ( uint16_t*) ps_calloc(112*64, sizeof(uint16_t));
      #endif
      
      tft.loadFont(valorax64);
      //tft.setTextColor(TFT_DARK_RED, TFT_BLACK);
      tft.setTextColor((uint16_t)strtol(timeColor.c_str(), NULL, 0), TFT_BLACK);
      
      middleCar = tft.textWidth(":");

      //If we have memory get the save the background
      //If not a beautiful rounded rectangle will replace it      
      if(screenContent!=nullptr) {
        //tft.drawRect( posXHourMiddle + middleCar, posYHour-8, 112, 64 , TFT_BLACK);//Just for debugging to see the region captured
        tft.readRect( posXHourMiddle + middleCar, posYHour-8, 112, 64 , screenContent);
      } else {
        tft.fillRoundRect(posXHourMiddle + middleCar ,posYHour-8 , 112, 64,3,TFT_BLACK);
      }
      
      //Get the position X after the draw of ":"
      posXHourRight = tft.drawString(":",posXHourMiddle,posYHour) + posXHourMiddle;
    
      char hoursString[4];
      sprintf(hoursString,"%02d",myhour);
      
      // get the width of the text in pixels
      int padding = tft.textWidth(hoursString);
      
      posXHourLeft = posXHourMiddle - padding;
      //Serial.printf("ShowHour: Left=%d - Mid=%d - Right=%d\n",posXHourLeft,posXHourMiddle,posXHourRight);
      
      tft.drawString(hoursString,posXHourLeft,posYHour);
      tft.unloadFont();
    }

    void showBME() {
      #ifdef USERMOD_BME280
        temperature = (BME->getTemperatureC() != 0)? (BME->getTemperatureC() + temperatureCorrection) : temperature;
        humidity = (BME->getHumidity() != 0) ? (BME->getHumidity() + humidityCorrection) : humidity;
      #endif

        tft.loadFont(Joystix16);
        //tft.setTextColor(TFT_MID_BLUE, TFT_BLACK,true);
        tft.setTextColor((uint16_t)strtol(BMEColor.c_str(), NULL, 0), TFT_BLACK,true);
        
        
        uint8_t BMEtextL = tft.textWidth("T:99");
        sprintf(BMEString, "T:%.2d", temperature );
                
        tft.fillRoundRect(2,posYBME-4,BMEtextL + 4,16+4,3,TFT_BLACK);
        //tft.drawRoundRect(2,posYBME-4,BMEtextL + 4,16+4,3,TFT_MID_BLUE);
        tft.drawRoundRect(2,posYBME-4,BMEtextL + 4,16+4,3,(uint16_t)strtol(BMEColor.c_str(), NULL, 0));
        tft.drawString(BMEString,4,posYBME);
        
        uint8_t BMEtextHumL = tft.textWidth("H:99");
        sprintf(BMEStringHum, "H:%.2d", humidity);
                
        tft.fillRoundRect(tft.width()-BMEtextHumL-6,posYBME-4,BMEtextHumL + 4,16+4,3,TFT_BLACK);
        //tft.drawRoundRect(tft.width()-BMEtextHumL-6,posYBME-4,BMEtextHumL + 4,16+4,3,TFT_MID_BLUE);
        tft.drawRoundRect(tft.width()-BMEtextHumL-6,posYBME-4,BMEtextHumL + 4,16+4,3,(uint16_t)strtol(BMEColor.c_str(), NULL, 0));

        tft.drawString(BMEStringHum,tft.width()-BMEtextHumL-4,posYBME);
        tft.unloadFont();        
    }
///DEBUG functions
    void showCardInfo() {
      #if defined (ESP32)
        Serial.print("ESP.getChipModel(); ");
        Serial.println(ESP.getChipModel());
        Serial.print("ESP.getSdkVersion(); ");
        Serial.println(ESP.getSdkVersion());
        Serial.print("ESP.getFlashChipSize(); ");
        Serial.println(ESP.getFlashChipSize());
        Serial.print("getFlashChipMode();");
        Serial.println(ESP.getFlashChipMode());
      #elif defined(ARDUINO_ARCH_ESP8266)
        Serial.print("ESP.getBootMode(); ");
        Serial.println(ESP.getBootMode());
        Serial.print("ESP.getSdkVersion(); ");
        Serial.println(ESP.getSdkVersion());
        Serial.print("ESP.getBootVersion(); ");
        Serial.println(ESP.getBootVersion());
        Serial.print("ESP.getChipId(); ");
        Serial.println(ESP.getChipId());
        Serial.print("ESP.getFlashChipSize(); ");
        Serial.println(ESP.getFlashChipSize());
        Serial.print("ESP.getFlashChipRealSize(); ");
        Serial.println(ESP.getFlashChipRealSize());
        Serial.print("ESP.getFlashChipSizeByChipId(); ");
        Serial.println(ESP.getFlashChipSizeByChipId());
        Serial.print("ESP.getFlashChipId(); ");
        Serial.println(ESP.getFlashChipId());
      #endif
      Serial.print("ESP.getFreeHeap(); ");
      Serial.println(ESP.getFreeHeap());
    }

    void printProcessorName(void)
    {
      Serial.print("Processor    = ");
      if ( user.esp == 0x8266) Serial.println("ESP8266");
      if ( user.esp == 0x32)   Serial.println("ESP32");
      if ( user.esp == 0x32F)  Serial.println("STM32");
      if ( user.esp == 0x2040) Serial.println("RP2040");
      if ( user.esp == 0x0000) Serial.println("Generic");
    }

    // Get pin name
    int8_t getPinName(int8_t pin)
    {
      // For ESP32 and RP2040 pin labels on boards use the GPIO number
      if (user.esp == 0x32 || user.esp == 0x2040) return pin;

      if (user.esp == 0x8266) {
        // For ESP8266 the pin labels are not the same as the GPIO number
        // These are for the NodeMCU pin definitions:
        //        GPIO       Dxx
        if (pin == 16) return 0;
        if (pin ==  5) return 1;
        if (pin ==  4) return 2;
        if (pin ==  0) return 3;
        if (pin ==  2) return 4;
        if (pin == 14) return 5;
        if (pin == 12) return 6;
        if (pin == 13) return 7;
        if (pin == 15) return 8;
        if (pin ==  3) return 9;
        if (pin ==  1) return 10;
        if (pin ==  9) return 11;
        if (pin == 10) return 12;
      }

      if (user.esp == 0x32F) return pin;

      return pin; // Invalid pin
    }
    void getTFTSettings() {
      tft.getSetup(user); //

      Serial.print("\n[code]\n");

      Serial.print ("TFT_eSPI ver = "); Serial.println(user.version);
      printProcessorName();
      #if defined (ESP32) || defined (ARDUINO_ARCH_ESP8266)
        if (user.esp < 0x32F000 || user.esp > 0x32FFFF) { Serial.print("Frequency    = "); Serial.print(ESP.getCpuFreqMHz());Serial.println("MHz"); }
      #endif
      #ifdef ARDUINO_ARCH_ESP8266
        Serial.print("Voltage      = "); Serial.print(ESP.getVcc() / 918.0); Serial.println("V"); // 918 empirically determined
      #endif
      Serial.print("Transactions = "); Serial.println((user.trans  ==  1) ? "Yes" : "No");
      Serial.print("Interface    = "); Serial.println((user.serial ==  1) ? "SPI" : "Parallel");
      #ifdef ARDUINO_ARCH_ESP8266
      if (user.serial ==  1){ Serial.print("SPI overlap  = "); Serial.println((user.overlap == 1) ? "Yes\n" : "No\n"); }
      #endif
      if (user.tft_driver != 0xE9D) // For ePaper displays the size is defined in the sketch
      {
        Serial.print("Display driver = "); Serial.println(user.tft_driver, HEX); // Hexadecimal code
        Serial.print("Display width  = "); Serial.println(user.tft_width);  // Rotation 0 width and height
        Serial.print("Display height = "); Serial.println(user.tft_height);
        Serial.println();
      }
      else if (user.tft_driver == 0xE9D) Serial.println("Display driver = ePaper\n");

      if (user.r0_x_offset  != 0)  { Serial.print("R0 x offset = "); Serial.println(user.r0_x_offset); } // Offsets, not all used yet
      if (user.r0_y_offset  != 0)  { Serial.print("R0 y offset = "); Serial.println(user.r0_y_offset); }
      if (user.r1_x_offset  != 0)  { Serial.print("R1 x offset = "); Serial.println(user.r1_x_offset); }
      if (user.r1_y_offset  != 0)  { Serial.print("R1 y offset = "); Serial.println(user.r1_y_offset); }
      if (user.r2_x_offset  != 0)  { Serial.print("R2 x offset = "); Serial.println(user.r2_x_offset); }
      if (user.r2_y_offset  != 0)  { Serial.print("R2 y offset = "); Serial.println(user.r2_y_offset); }
      if (user.r3_x_offset  != 0)  { Serial.print("R3 x offset = "); Serial.println(user.r3_x_offset); }
      if (user.r3_y_offset  != 0)  { Serial.print("R3 y offset = "); Serial.println(user.r3_y_offset); }

      if (user.pin_tft_mosi != -1) { Serial.print("MOSI    = "); Serial.print("GPIO "); Serial.println(getPinName(user.pin_tft_mosi)); }
      if (user.pin_tft_miso != -1) { Serial.print("MISO    = "); Serial.print("GPIO "); Serial.println(getPinName(user.pin_tft_miso)); }
      if (user.pin_tft_clk  != -1) { Serial.print("SCK     = "); Serial.print("GPIO "); Serial.println(getPinName(user.pin_tft_clk)); }

      #ifdef ARDUINO_ARCH_ESP8266
      if (user.overlap == true)
      {
        Serial.println("Overlap selected, following pins MUST be used:");

                                  Serial.println("MOSI     = SD1 (GPIO 8)");
                                  Serial.println("MISO     = SD0 (GPIO 7)");
                                  Serial.println("SCK      = CLK (GPIO 6)");
                                  Serial.println("TFT_CS   = D3  (GPIO 0)\n");

        Serial.println("TFT_DC and TFT_RST pins can be user defined");
      }
      #endif
      String pinNameRef = "GPIO ";
      #ifdef ARDUINO_ARCH_ESP8266
        pinNameRef = "PIN_D";
      #endif

      if (user.esp == 0x32F) {
        Serial.println("\n>>>>> Note: STM32 pin references above D15 may not reflect board markings <<<<<");
        pinNameRef = "D";
      }
      if (user.pin_tft_cs != -1) { Serial.print("TFT_CS   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_cs)); }
      if (user.pin_tft_dc != -1) { Serial.print("TFT_DC   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_dc)); }
      if (user.pin_tft_rst!= -1) { Serial.print("TFT_RST  = " + pinNameRef); Serial.println(getPinName(user.pin_tft_rst)); }

      if (user.pin_tch_cs != -1) { Serial.print("TOUCH_CS = " + pinNameRef); Serial.println(getPinName(user.pin_tch_cs)); }

      if (user.pin_tft_wr != -1) { Serial.print("TFT_WR   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_wr)); }
      if (user.pin_tft_rd != -1) { Serial.print("TFT_RD   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_rd)); }

      if (user.pin_tft_d0 != -1) { Serial.print("\nTFT_D0   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d0)); }
      if (user.pin_tft_d1 != -1) { Serial.print("TFT_D1   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d1)); }
      if (user.pin_tft_d2 != -1) { Serial.print("TFT_D2   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d2)); }
      if (user.pin_tft_d3 != -1) { Serial.print("TFT_D3   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d3)); }
      if (user.pin_tft_d4 != -1) { Serial.print("TFT_D4   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d4)); }
      if (user.pin_tft_d5 != -1) { Serial.print("TFT_D5   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d5)); }
      if (user.pin_tft_d6 != -1) { Serial.print("TFT_D6   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d6)); }
      if (user.pin_tft_d7 != -1) { Serial.print("TFT_D7   = " + pinNameRef); Serial.println(getPinName(user.pin_tft_d7)); }

      #if defined (TFT_BL)
        Serial.print("\nTFT_BL           = " + pinNameRef); Serial.println(getPinName(user.pin_tft_led));
        #if defined (TFT_BACKLIGHT_ON)
          Serial.print("TFT_BACKLIGHT_ON = "); Serial.println(user.pin_tft_led_on == HIGH ? "HIGH" : "LOW");
        #endif
      #endif

      Serial.println();

      uint16_t fonts = tft.fontsLoaded();
      if (fonts & (1 << 1))        Serial.print("Font GLCD   loaded\n");
      if (fonts & (1 << 2))        Serial.print("Font 2      loaded\n");
      if (fonts & (1 << 4))        Serial.print("Font 4      loaded\n");
      if (fonts & (1 << 6))        Serial.print("Font 6      loaded\n");
      if (fonts & (1 << 7))        Serial.print("Font 7      loaded\n");
      if (fonts & (1 << 9))        Serial.print("Font 8N     loaded\n");
      else
      if (fonts & (1 << 8))        Serial.print("Font 8      loaded\n");
      if (fonts & (1 << 15))       Serial.print("Smooth font enabled\n");
      Serial.print("\n");

      if (user.serial==1)        { Serial.print("Display SPI frequency = "); Serial.println(user.tft_spi_freq/10.0); }
      if (user.pin_tch_cs != -1) { Serial.print("Touch SPI frequency   = "); Serial.println(user.tch_spi_freq/10.0); }

      Serial.println("[/code]");      
    }
  public:

    // non WLED related methods, may be used for data exchange between usermods (non-inline methods should be defined out of class)

    /**
     * Enable/Disable the usermod
     */
    inline void enable(bool enable) { enabled = enable; }

    /**
     * Get usermod enabled/disabled state
     */
    inline bool isEnabled() { return enabled; }
    // methods called by WLED (can be inlined as they are called only once but if you call them explicitly define them out of class)

    /*
     * setup() is called once at boot. WiFi is not yet connected at this point.
     * readFromConfig() is called prior to setup()
     * You can use it to initialize variables, sensors or similar.
     */
    void setup() {
      // do your set-up here
      Serial.begin(115200);
      //while (! Serial);///Never do you need to wait a serial connection to start or you want just debug :)
      showCardInfo();
      Serial.println("======================================================");
      Serial.println("|         Hello from CyberPunk Clock usermod !       |");
      Serial.println("======================================================");
      Serial.println("TFT Config");
      Serial.printf("TFT_MISO[%d] - TFT_MOSI[%d] - TFT_SCLK[%d] - TFT_CS [%d] - TFT_DC[%d] - TFT_RST[%d]\n",
        TFT_MISO,TFT_MOSI,TFT_SCLK,TFT_CS,TFT_DC,TFT_RST);
      Serial.printf("SD_CS[%d]\n",SD_CS);
      Serial.printf("User mods Enabled : %d\n",enabled);
      getTFTSettings();
      
      if (!enabled) return;
      // Set all chip selects high to avoid bus contention during initialisation of each peripheral
      digitalWrite(TFT_CS, HIGH); // TFT screen chip select
      digitalWrite(SD_CS, HIGH); // SD card chips select
      digitalWrite(TOUCH_CS, HIGH); // Touch chips select

      tft.init();

      uint16_t calData[5] = { 517, 3294, 424, 3229, 7 };//Do the calibration program to get this data https://github.com/Bodmer/TFT_eSPI/tree/master/examples/Generic/Touch_calibrate
      tft.setTouch(calData);

      sdCardEnabled = SD.begin(SD_CS);

      if(!sdCardEnabled){
        Serial.println("Card Mount Failed");
      }
      
      tft.setRotation(3);  // portrait
      
      tft.setTextDatum(MC_DATUM);      
      posXHourMiddle = (tft.width()/2)-16;

      //Serial.printf("SETUP WIDTH %d - Middle %d\n",tft.width(),posXHourMiddle);

      targetTime = millis() + 1000;
      
      #ifdef USERMOD_BME280
        BME = (UsermodBME280*) usermods.lookup(USERMOD_ID_BME280);
      #endif
      
      //try to get updated date & time
      updateLocalTime();
      //Start with a background
      showHour(hh);
      ohh=hh;
      
      initDone = true;
      Serial.printf("Initialisation usermods initDone [%d]\n",initDone);
    }


    /*
     * connected() is called every time the WiFi is (re)connected
     * Use it to initialize network interfaces
     */
    void connected() {
      //Serial.println("Connected to WiFi!");
    }


    /*
     * loop() is called continuously. Here you can check for events, read sensors, etc.
     * 
     * Tips:
     * 1. You can use "if (WLED_CONNECTED)" to check for a successful network connection.
     *    Additionally, "if (WLED_MQTT_CONNECTED)" is available to check for a connection to an MQTT broker.
     * 
     * 2. Try to avoid using the delay() function. NEVER use delays longer than 10 milliseconds.
     *    Instead, use a timer check as shown here.
     */
    void loop() {
      // if usermod is disabled or called during strip updating just exit
      // NOTE: on very long strips strip.isUpdating() may always return true so update accordingly
      if (!enabled || strip.isUpdating()) return;

      if (targetTime < millis()) {
        // Set next update for 1 second later
        targetTime = millis() + 1000;

        if(year(localTime)!=1970) {
          updateLocalTime();
          hh=hour(localTime);
          mm=minute(localTime);
          ss=second(localTime);
        } else {
          // Adjust the time values by adding 1 second
          ss++;              // Advance second
          if (ss == 60) {    // Check for roll-over
            ss = 0;          // Reset seconds to zero
            omm = mm;        // Save last minute time for display update
            mm++;            // Advance minute
            if (mm > 59) {   // Check for roll-over
              mm = 0;
              hh++;          // Advance hour
              if (hh > 23) { // Check for 24hr roll-over (could roll-over on 13)
                hh = 0;      // 0 for 24 hour clock, set to 1 for 12 hour clock
              }
            }
          }
        }
        
        //Serial.printf("Time : [%02d:%02d:%02d]\n",hh,mm,ss);

        if(ohh!=hh) {
          ohh=hh;
          showHour(hh);
        }
        
        tft.setTextDatum(TL_DATUM);//All Coordinates "Top left" !!!        

        tft.loadFont(valorax64);
        
        if (oss != ss) { // Redraw seconds time every second
          oss = ss;
          if(ss%2)
            tft.setTextColor((uint16_t)strtol(dateColor.c_str(), NULL, 0), TFT_BLACK);
          else
            tft.setTextColor((uint16_t)strtol(timeColor.c_str(), NULL, 0), TFT_BLACK);
        }
          
        tft.drawString(":",posXHourMiddle,posYHour);
      
        if (omm != mm) { // Redraw hours and minutes time every minute
          omm = mm;
          sprintf(minutesString,"%02d",mm);
          tft.setTextColor((uint16_t)strtol(timeColor.c_str(), NULL, 0), TFT_BLACK);
          if(screenContent!=nullptr)
            tft.pushRect( posXHourMiddle + middleCar, posYHour-8, 112, 64 , screenContent);
          else
            tft.fillRoundRect(posXHourMiddle + middleCar ,posYHour-8 , 112, 64,3,TFT_BLACK);
          tft.drawString(minutesString,posXHourRight,posYHour);
        }
        tft.unloadFont();
        
        //BME Infos Only 10s
        if(ss%10==0) {
          #ifdef USERMOD_BME280
            if(BME!=nullptr) {
              showBME();
            }
          #endif            
        }

      }//Target
      //Instant check
      uint16_t x, y;
      bool pressed = tft.getTouch(&x, &y);
      if(pressed) {
        delay(500);//Try to avoid to many calls
        Serial.printf("BRI: %d\n", bri);        
        if(bri==0) {
          bri = currentBri;
        } else {
          currentBri = bri;
          bri=0;            
        }
        stateUpdated(CALL_MODE_NOTIFICATION);      
        
        //Just for debug, perhaps in the future I will handle specific zone to change other thing than light on/off
        Serial.printf("x: %i     ", x);
        Serial.printf("y: %i     ", y);
        Serial.printf("z: %i \n", tft.getTouchRawZ());
        
      }

    }


    /*
     * addToJsonInfo() can be used to add custom entries to the /json/info part of the JSON API.
     * Creating an "u" object allows you to add custom key/value pairs to the Info section of the WLED web UI.
     * Below it is shown how this could be used for e.g. a light sensor
     */
    void addToJsonInfo(JsonObject& root)
    {
      // if "u" object does not exist yet wee need to create it
      JsonObject user = root["u"];
      if (user.isNull()) user = root.createNestedObject("u");
      JsonArray lightArr = user.createNestedArray("CPC"); //name
      lightArr.add(enabled?F("installed"):F("disabled")); //unit
    }


    /*
     * addToJsonState() can be used to add custom entries to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void addToJsonState(JsonObject& root)
    {
      if (!initDone || !enabled) return;  // prevent crash on boot applyPreset()

      JsonObject usermod = root[FPSTR(_name)];
      if (usermod.isNull()) usermod = root.createNestedObject(FPSTR(_name));

      //usermod["user0"] = userVar0;
    }


    /*
     * readFromJsonState() can be used to receive data clients send to the /json/state part of the JSON API (state object).
     * Values in the state object may be modified by connected clients
     */
    void readFromJsonState(JsonObject& root)
    {
      if (!initDone) return;  // prevent crash on boot applyPreset()

      JsonObject usermod = root[FPSTR(_name)];
      if (!usermod.isNull()) {
        // expect JSON usermod data in usermod name object: {"ExampleUsermod:{"user0":10}"}
        //userVar0 = usermod["user0"] | userVar0; //if "user0" key exists in JSON, update, else keep old value
      }
      // you can as well check WLED state JSON keys
      //if (root["bri"] == 255) Serial.println(F("Don't burn down your garage!"));
    }


    /*
     * addToConfig() can be used to add custom persistent settings to the cfg.json file in the "um" (usermod) object.
     * It will be called by WLED when settings are actually saved (for example, LED settings are saved)
     * If you want to force saving the current state, use serializeConfig() in your loop().
     * 
     * CAUTION: serializeConfig() will initiate a filesystem write operation.
     * It might cause the LEDs to stutter and will cause flash wear if called too often.
     * Use it sparingly and always in the loop, never in network callbacks!
     * 
     * addToConfig() will make your settings editable through the Usermod Settings page automatically.
     *
     * Usermod Settings Overview:
     * - Numeric values are treated as floats in the browser.
     *   - If the numeric value entered into the browser contains a decimal point, it will be parsed as a C float
     *     before being returned to the Usermod.  The float data type has only 6-7 decimal digits of precision, and
     *     doubles are not supported, numbers will be rounded to the nearest float value when being parsed.
     *     The range accepted by the input field is +/- 1.175494351e-38 to +/- 3.402823466e+38.
     *   - If the numeric value entered into the browser doesn't contain a decimal point, it will be parsed as a
     *     C int32_t (range: -2147483648 to 2147483647) before being returned to the usermod.
     *     Overflows or underflows are truncated to the max/min value for an int32_t, and again truncated to the type
     *     used in the Usermod when reading the value from ArduinoJson.
     * - Pin values can be treated differently from an integer value by using the key name "pin"
     *   - "pin" can contain a single or array of integer values
     *   - On the Usermod Settings page there is simple checking for pin conflicts and warnings for special pins
     *     - Red color indicates a conflict.  Yellow color indicates a pin with a warning (e.g. an input-only pin)
     *   - Tip: use int8_t to store the pin value in the Usermod, so a -1 value (pin not set) can be used
     *
     * See usermod_v2_auto_save.h for an example that saves Flash space by reusing ArduinoJson key name strings
     * 
     * If you need a dedicated settings page with custom layout for your Usermod, that takes a lot more work.  
     * You will have to add the setting to the HTML, xml.cpp and set.cpp manually.
     * See the WLED Soundreactive fork (code and wiki) for reference.  https://github.com/atuline/WLED
     * 
     * I highly recommend checking out the basics of ArduinoJson serialization and deserialization in order to use custom settings!
     */
    void addToConfig(JsonObject& root)
    {
      JsonObject top = root.createNestedObject(FPSTR(_name));
      //save these vars persistently whenever settings are saved
      top[FPSTR(_enabled)] = enabled;
      top["DatePosition"] = posYDate;
      top["TimePosition"] = posYHour;
      top["BMEPosition"] = posYBME;
      top["DateColor"] = dateColor;
      top["TimeColor"] = timeColor;
      top["BMEColor"] = BMEColor;
      top["TemperatureCorrection"] = temperatureCorrection;
      top["HumidityCorrection"] = humidityCorrection;
    }


    /*
     * readFromConfig() can be used to read back the custom settings you added with addToConfig().
     * This is called by WLED when settings are loaded (currently this only happens immediately after boot, or after saving on the Usermod Settings page)
     * 
     * readFromConfig() is called BEFORE setup(). This means you can use your persistent values in setup() (e.g. pin assignments, buffer sizes),
     * but also that if you want to write persistent values to a dynamic buffer, you'd need to allocate it here instead of in setup.
     * If you don't know what that is, don't fret. It most likely doesn't affect your use case :)
     * 
     * Return true in case the config values returned from Usermod Settings were complete, or false if you'd like WLED to save your defaults to disk (so any missing values are editable in Usermod Settings)
     * 
     * getJsonValue() returns false if the value is missing, or copies the value into the variable provided and returns true if the value is present
     * The configComplete variable is true only if the "exampleUsermod" object and all values are present.  If any values are missing, WLED will know to call addToConfig() to save them
     * 
     * This function is guaranteed to be called on boot, but could also be called every time settings are updated
     */
    bool readFromConfig(JsonObject& root)
    {
      // default settings values could be set here (or below using the 3-argument getJsonValue()) instead of in the class definition or constructor
      // setting them inside readFromConfig() is slightly more robust, handling the rare but plausible use case of single value being missing after boot (e.g. if the cfg.json was manually edited and a value was removed)

      JsonObject top = root[FPSTR(_name)];

      bool configComplete = !top.isNull();
      configComplete &= getJsonValue(top[FPSTR(_enabled)], enabled);
      configComplete &= getJsonValue(top["DatePosition"], posYDate, 24);
      configComplete &= getJsonValue(top["TimePosition"], posYHour, 96);
      configComplete &= getJsonValue(top["BMEPosition"], posYBME, 208);
      configComplete &= getJsonValue(top["DateColor"], dateColor, "0x0320");
      configComplete &= getJsonValue(top["TimeColor"], timeColor, 0xA800);
      configComplete &= getJsonValue(top["BMEColor"], BMEColor, "0x3231");
      configComplete &= getJsonValue(top["TemperatureCorrection"], temperatureCorrection, 0);
      configComplete &= getJsonValue(top["HumidityCorrection"], humidityCorrection, 0);

      return configComplete;
    }


    /*
     * appendConfigData() is called when user enters usermod settings page
     * it may add additional metadata for certain entry fields (adding drop down is possible)
     * be careful not to add too much as oappend() buffer is limited to 3k
     */
    void appendConfigData()
    {
      /*
      oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":great")); oappend(SET_F("',1,'<i>(this is a great config value)</i>');"));
      oappend(SET_F("addInfo('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F(":testString")); oappend(SET_F("',1,'enter any string you want');"));
      oappend(SET_F("dd=addDropdown('")); oappend(String(FPSTR(_name)).c_str()); oappend(SET_F("','testInt');"));
      oappend(SET_F("addOption(dd,'Nothing',0);"));
      oappend(SET_F("addOption(dd,'Everything',42);"));
      */
    }


    /*
     * handleOverlayDraw() is called just before every show() (LED strip update frame) after effects have set the colors.
     * Use this to blank out some LEDs or set them to a different color regardless of the set effect mode.
     * Commonly used for custom clocks (Cronixie, 7 segment)
     */
    void handleOverlayDraw()
    {
      //strip.setPixelColor(0, RGBW32(0,0,0,0)) // set the first pixel to black
    }


    /**
     * handleButton() can be used to override default button behaviour. Returning true
     * will prevent button working in a default way.
     * Replicating button.cpp
     */
    bool handleButton(uint8_t b) {
      yield();
      return false;
      /*
      yield();
      // ignore certain button types as they may have other consequences
      if (!enabled
       || buttonType[b] == BTN_TYPE_NONE
       || buttonType[b] == BTN_TYPE_RESERVED
       || buttonType[b] == BTN_TYPE_PIR_SENSOR
       || buttonType[b] == BTN_TYPE_ANALOG
       || buttonType[b] == BTN_TYPE_ANALOG_INVERTED) {
        return false;
      }

      bool handled = false;
      // do your button handling here
      return handled;
      */
    }
  

#ifndef WLED_DISABLE_MQTT
    /**
     * handling of MQTT message
     * topic only contains stripped topic (part after /wled/MAC)
     */
    bool onMqttMessage(char* topic, char* payload) {
      // check if we received a command
      //if (strlen(topic) == 8 && strncmp_P(topic, PSTR("/command"), 8) == 0) {
      //  String action = payload;
      //  if (action == "on") {
      //    enabled = true;
      //    return true;
      //  } else if (action == "off") {
      //    enabled = false;
      //    return true;
      //  } else if (action == "toggle") {
      //    enabled = !enabled;
      //    return true;
      //  }
      //}
      return false;
    }

    /**
     * onMqttConnect() is called when MQTT connection is established
     */
    void onMqttConnect(bool sessionPresent) {
      // do any MQTT related initialisation here
      //publishMqtt("I am alive!");
    }
#endif


    /**
     * onStateChanged() is used to detect WLED state change
     * @mode parameter is CALL_MODE_... parameter used for notifications
     */
    void onStateChange(uint8_t mode) {
      // do something if WLED state changed (color, brightness, effect, preset, etc)
    }


    /*
     * getId() allows you to optionally give your V2 usermod an unique ID (please define it in const.h!).
     * This could be used in the future for the system to determine whether your usermod is installed.
     */
    uint16_t getId()
    {
      return USERMOD_ID_CPC;
    }

   //More methods can be added in the future, this example will then be extended.
   //Your usermod will remain compatible as it does not need to implement all methods from the Usermod base class!
};


// add more strings here to reduce flash memory usage
const char CPC::_name[]    PROGMEM = "CyberPunk Clock";
const char CPC::_enabled[] PROGMEM = "enabled";


// implementation of non-inline member methods
void CPC::publishMqtt(const char* state, bool retain)
{
#ifndef WLED_DISABLE_MQTT
  //Check if MQTT Connected, otherwise it will crash the 8266
  if (WLED_MQTT_CONNECTED) {
    char subuf[64];
    strcpy(subuf, mqttDeviceTopic);
    strcat_P(subuf, PSTR("/example"));
    mqtt->publish(subuf, 0, retain, state);
  }
#endif
}