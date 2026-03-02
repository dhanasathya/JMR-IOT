// Final combined sketch: JMR iFarm Controller (V5 - Back button logic fixed for Water Setup)
// - FIX 1: showTimes() function included.
// - FIX 2: BACK button is now visible and functional on all pages involved in Top/Bottom water setup (Modes, Time Select, Duration Edit).
// - FIX 3: Back button press in SECONDS editing mode correctly returns to MINUTES editing mode.
// - FIX 4: Each time slot has completely independent START H/M for MINUTE duration logic and SECOND duration logic.

#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <TouchScreen.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>

// ---------------- Touch Calibration Values (update with your calibrated values) ----------------
const int TS_LEFT  = 155;
const int TS_RT    = 917;
const int TS_TOP   = 951;
const int TS_BOT   = 87;

MCUFRIEND_kbv tft;
RTC_DS3231 rtc;

// ---------------- Colors ----------------
#define BLACK  0x0000
#define WHITE  0xFFFF
#define YELLOW 0xFFE0
#define GREEN  0x07E0
#define BLUE   0x001F // Used for SEC button background

// ---------------- Touchscreen config ----------------
// XP, YP, XM, YM – adjust to your shield wiring if needed.
TouchScreen ts = TouchScreen(8, A3, A2, 9, 300);

#define MINPRESSURE 200
#define MAXPRESSURE 1000
int pixel_x, pixel_y;
bool lastTouch = false;

// ---------------- Pages ----------------
// 1=Home, 2=Menu, 3=Moisture (Manual UI for Top OR Bottom),
// 4=Timer Bottom (AUTO slot edit), 5=Timer Fan/TopAuto (AUTO slot edit), 6=Timer Light,
// 7=TopWater Mode (AUTO/MANUAL), 8=BottomWater Mode (AUTO/MANUAL)
// 9 = Top AUTO -> TIME SELECT (TIME1, TIME2)
// 10 = Bottom AUTO -> TIME SELECT (TIME1, TIME2)
int page = 1;
int previousPage = 1; // Stores the page before the current one

// ---------------- Moisture & Cutoffs ----------------
const int moisturePin = A8;
int topCutoff = 40;
int bottomCutoff = 40;

// Which device the Moisture page (page=3) is currently editing:
// 0=none, 1=TopWater, 2=BottomWater
uint8_t manualTarget = 0;

// Manual OK latches
bool manualTopOKRequested = false;
bool manualBottomOKRequested = false;

// ---------------- Timers (Legacy & Clock-based) ----------------
// Legacy timers (Bottom Water, Fan, Light) are kept for storage compatibility
int onH2=6, onM2=0;
int offH2=18, offM2=0;

int onH3=6, onM3=0; // Fan timer
int offH3=18, offM3=0;

int onH4=6, onM4=0; // Light timer
int offH4=18, offM4=0;

// legacy TopWater AUTO start (single pair) kept for compatibility
int onH_TA=6, onM_TA=0;
int offH_TA=18, offM_TA=0;

// Duration minutes for legacy Top/Bottom AUTO (kept for storage)
int durTopAutoMin = 10;
int durBottomAutoMin = 10;

// ---------------- NEW: Two TIME slots per Top/Bottom (Independent Durations and START Times) ----------------

// Top TIME1 Start Time (Separate for MIN and SEC modes)
int onH_T1_Min = 6,  onM_T1_Min = 0;
int onH_T1_Sec = 6,  onM_T1_Sec = 0;
int durMin_T1  = 10; // 0..240 MIN
int durSec_T1  = 0;  // 0..240 SEC

// Top TIME2
int onH_T2_Min = 18, onM_T2_Min = 0;
int onH_T2_Sec = 18, onM_T2_Sec = 0;
int durMin_T2  = 10;
int durSec_T2  = 0;

// Bottom TIME1
int onH_B1_Min = 7,  onM_B1_Min = 0;
int onH_B1_Sec = 7,  onM_B1_Sec = 0;
int durMin_B1  = 10;
int durSec_B1  = 0;

// Bottom TIME2
int onH_B2_Min = 19, onM_B2_Min = 0;
int onH_B2_Sec = 19, onM_B2_Sec = 0;
int durMin_B2  = 10;
int durSec_B2  = 0;

// Modes
bool topWaterAuto    = false; // true=AUTO, false=MANUAL
bool bottomWaterAuto = true;  // true=AUTO, false=MANUAL

// When page==5, this flag tells if that page is editing TopWater-AUTO (legacy behavior)
bool editingTopAuto = false;

// When using timer UI for START+DURATION editing, this selects which slot we are editing:
// 0 = none, 1 = Top TIME1, 2 = Top TIME2, 3 = Bottom TIME1, 4 = Bottom TIME2
int editAutoSlot = 0;

// Duration Mode for editing (TRUE if currently showing/editing SECONDS duration/start time)
bool durationInSeconds = false;

// ---------------- Layout constants ----------------
const int centerX=160, leftX=60, rightX=260;
const int plusMinusOffset=50;
const int startY=140, stopY=340, btnR=25;
const int boxW=140, boxH=50;

// New button dimensions for HOME/BACK/MENU row
const int btnY = 420;
const int btnH = 40;
const int btnW = 90;
const int btnSpacing = 10;
const int homeX = 10;
const int backX = homeX + btnW + btnSpacing;
const int menuX = backX + btnW + btnSpacing;

// SEC button position (Top Right Corner)
const int secBtnX = 265;
const int secBtnY = 10;
const int secBtnW = 50;
const int secBtnH = 40;

// ---------------- Relays ----------------
const int relayTop   = 22; // Shared: Top watering; also ON when Bottom runs
const int relayBottom= 24; // Bottom watering second relay
const int relayFan   = 26;
const int relayLight = 28;

// ---------------- RTC refresh ----------------
unsigned long lastRTCupdate=0;

/* ---------------- Touch ----------------- */
bool Touch_getXY() {
  TSPoint p = ts.getPoint();

  // Restore shared pins immediately after read (critical for MCUFRIEND shields)
  pinMode(A3, OUTPUT);   // YP
  pinMode(A2, OUTPUT);   // XM
  digitalWrite(A3, HIGH);
  digitalWrite(A2, HIGH);

  if (p.z > MINPRESSURE && p.z < MAXPRESSURE) {
    // Map raw touch to screen pixels (portrait 320x480)
    pixel_x = map(p.x, TS_LEFT, TS_RT, 0, 320);
    pixel_y = map(p.y, TS_TOP,  TS_BOT, 0, 480);

    // Debug
    // Serial.print("X: "); Serial.print(pixel_x);
    // Serial.print(" Y: "); Serial.print(pixel_y);
    // Serial.print(" Z: "); Serial.println(p.z);

    return true;
  }
  return false;
}

/* ---------------- EEPROM ---------------- */
void saveSettings() {
  EEPROM.update(0, (uint8_t)topCutoff);

  // Legacy timers (1-16)
  EEPROM.update(1, (uint8_t)onH2);  EEPROM.update(2, (uint8_t)onM2);  EEPROM.update(3, (uint8_t)offH2);  EEPROM.update(4, (uint8_t)offM2);
  EEPROM.update(5, (uint8_t)onH3);  EEPROM.update(6, (uint8_t)onM3);  EEPROM.update(7, (uint8_t)offH3);  EEPROM.update(8, (uint8_t)offM3);
  EEPROM.update(9, (uint8_t)onH4);  EEPROM.update(10, (uint8_t)onM4); EEPROM.update(11, (uint8_t)offH4); EEPROM.update(12, (uint8_t)offM4);

  EEPROM.update(13, (uint8_t)onH_TA);  EEPROM.update(14, (uint8_t)onM_TA);
  EEPROM.update(15, (uint8_t)offH_TA); EEPROM.update(16, (uint8_t)offM_TA);

  EEPROM.update(17, (uint8_t)bottomCutoff);

  EEPROM.update(18, (uint8_t)durTopAutoMin);
  EEPROM.update(19, (uint8_t)durBottomAutoMin);

  // NEW slots (StartH_Min, StartM_Min, StartH_Sec, StartM_Sec, DurMin, DurSec)
  // T1 (20-25)
  EEPROM.update(20, (uint8_t)onH_T1_Min); EEPROM.update(21, (uint8_t)onM_T1_Min);
  EEPROM.update(22, (uint8_t)onH_T1_Sec); EEPROM.update(23, (uint8_t)onM_T1_Sec);
  EEPROM.update(24, (uint8_t)durMin_T1);  EEPROM.update(25, (uint8_t)durSec_T1);

  // T2 (26-31)
  EEPROM.update(26, (uint8_t)onH_T2_Min); EEPROM.update(27, (uint8_t)onM_T2_Min);
  EEPROM.update(28, (uint8_t)onH_T2_Sec); EEPROM.update(29, (uint8_t)onM_T2_Sec);
  EEPROM.update(30, (uint8_t)durMin_T2);  EEPROM.update(31, (uint8_t)durSec_T2);

  // B1 (32-37)
  EEPROM.update(32, (uint8_t)onH_B1_Min); EEPROM.update(33, (uint8_t)onM_B1_Min);
  EEPROM.update(34, (uint8_t)onH_B1_Sec); EEPROM.update(35, (uint8_t)onM_B1_Sec);
  EEPROM.update(36, (uint8_t)durMin_B1);  EEPROM.update(37, (uint8_t)durSec_B1);

  // B2 (38-43)
  EEPROM.update(38, (uint8_t)onH_B2_Min); EEPROM.update(39, (uint8_t)onM_B2_Min);
  EEPROM.update(40, (uint8_t)onH_B2_Sec); EEPROM.update(41, (uint8_t)onM_B2_Sec);
  EEPROM.update(42, (uint8_t)durMin_B2);  EEPROM.update(43, (uint8_t)durSec_B2);
}

void loadSettings() {
  uint8_t v;

  v = EEPROM.read(0);  if (v <= 100) topCutoff = v;

  // Load legacy timers (1-16)
  v = EEPROM.read(1);  if (v <= 23) onH2 = v;  v = EEPROM.read(2);  if (v <= 59) onM2 = v;
  v = EEPROM.read(3);  if (v <= 23) offH2 = v; v = EEPROM.read(4);  if (v <= 59) offM2 = v;
  v = EEPROM.read(5);  if (v <= 23) onH3 = v;  v = EEPROM.read(6);  if (v <= 59) onM3 = v;
  v = EEPROM.read(7);  if (v <= 23) offH3 = v; v = EEPROM.read(8);  if (v <= 59) offM3 = v;
  v = EEPROM.read(9);  if (v <= 23) onH4 = v;  v = EEPROM.read(10); if (v <= 59) onM4 = v;
  v = EEPROM.read(11); if (v <= 23) offH4 = v; v = EEPROM.read(12); if (v <= 59) offM4 = v;
  v = EEPROM.read(13); if (v <= 23) onH_TA = v; v = EEPROM.read(14); if (v <= 59) onM_TA = v;
  v = EEPROM.read(15); if (v <= 23) offH_TA = v; v = EEPROM.read(16); if (v <= 59) offM_TA = v;

  v = EEPROM.read(17); if (v <= 100) bottomCutoff = v;

  v = EEPROM.read(18); if (v <= 240) durTopAutoMin = v;
  v = EEPROM.read(19); if (v <= 240) durBottomAutoMin = v;

  // Load NEW slots (H_Min, M_Min, H_Sec, M_Sec, DurMin, DurSec)
  // T1 (20-25)
  v = EEPROM.read(20); if (v <= 23) onH_T1_Min = v; v = EEPROM.read(21); if (v <= 59) onM_T1_Min = v;
  v = EEPROM.read(22); if (v <= 23) onH_T1_Sec = v; v = EEPROM.read(23); if (v <= 59) onM_T1_Sec = v;
  v = EEPROM.read(24); if (v <= 240) durMin_T1 = v;  v = EEPROM.read(25); if (v <= 240) durSec_T1 = v;

  // T2 (26-31)
  v = EEPROM.read(26); if (v <= 23) onH_T2_Min = v; v = EEPROM.read(27); if (v <= 59) onM_T2_Min = v;
  v = EEPROM.read(28); if (v <= 23) onH_T2_Sec = v; v = EEPROM.read(29); if (v <= 59) onM_T2_Sec = v;
  v = EEPROM.read(30); if (v <= 240) durMin_T2 = v;  v = EEPROM.read(31); if (v <= 240) durSec_T2 = v;

  // B1 (32-37)
  v = EEPROM.read(32); if (v <= 23) onH_B1_Min = v; v = EEPROM.read(33); if (v <= 59) onM_B1_Min = v;
  v = EEPROM.read(34); if (v <= 23) onH_B1_Sec = v; v = EEPROM.read(35); if (v <= 59) onM_B1_Sec = v;
  v = EEPROM.read(36); if (v <= 240) durMin_B1 = v;  v = EEPROM.read(37); if (v <= 240) durSec_B1 = v;

  // B2 (38-43)
  v = EEPROM.read(38); if (v <= 23) onH_B2_Min = v; v = EEPROM.read(39); if (v <= 59) onM_B2_Min = v;
  v = EEPROM.read(40); if (v <= 23) onH_B2_Sec = v; v = EEPROM.read(41); if (v <= 59) onM_B2_Sec = v;
  v = EEPROM.read(42); if (v <= 240) durMin_B2 = v;  v = EEPROM.read(43); if (v <= 240) durSec_B2 = v;

  if (topCutoff>100)    topCutoff=40;
  if (bottomCutoff>100) bottomCutoff=40;
}

/* ---------------- UI helpers ---------------- */
void drawRTCtime(){
  DateTime now = rtc.now();
  tft.fillRect(10,10,150,24,BLACK); // Clear area for RTC
  tft.setTextColor(YELLOW);
  tft.setTextSize(3);
  tft.setCursor(10,10);
  if(now.hour()<10) tft.print("0"); tft.print(now.hour()); tft.print(":");
  if(now.minute()<10) tft.print("0"); tft.print(now.minute()); tft.print(":");
  if(now.second()<10) tft.print("0"); tft.print(now.second());
}

void drawBtn(int x, int y, const char* label){
  tft.drawCircle(x, y, btnR, WHITE);
  tft.setTextColor(WHITE);
  tft.setTextSize(3);
  int dx=(label[0]=='+')?-8:-6;
  tft.setCursor(x+dx, y-10);
  tft.print(label);
}

// Helper to update page and previousPage
void setPage(int newPage){
  if(page != newPage){
    previousPage = page;
  }
  page = newPage;
}

// Draw the common HOME / BACK / MENU buttons
void drawHBM_Bar(){
  // BACK hidden ONLY on Menu page
  bool hideBack = (page == 2);

  // HOME
  tft.fillRect(homeX, btnY, btnW, btnH, YELLOW);
  tft.setTextColor(BLACK);
  tft.setTextSize(3);
  tft.setCursor(homeX + 10, btnY + 10);
  tft.print("HOME");

  // BACK
  if (!hideBack) {
    tft.fillRect(backX, btnY, btnW, btnH, YELLOW);
    tft.setTextColor(BLACK);
    tft.setCursor(backX + 15, btnY + 10);
    tft.print("BACK");
  } else {
    tft.fillRect(backX, btnY, btnW, btnH, BLACK);
  }

  // MENU
  tft.fillRect(menuX, btnY, btnW, btnH, YELLOW);
  tft.setTextColor(BLACK);
  tft.setCursor(menuX + 10, btnY + 10);
  tft.print("MENU");
}

// For LEGACY clock timers (Fan/Light): show START time and STOP time.
void showTimes(){
  int onH, onM, offH, offM;
  if(page==5 && editAutoSlot==0){ // FAN
    onH=onH3; onM=onM3; offH=offH3; offM=offM3;
  } else if(page==6){ // LIGHT
    onH=onH4; onM=onM4; offH=offH4; offM=offM4;
  } else {
    return;
  }

  tft.setTextColor(WHITE);
  tft.setTextSize(3);

  // START time box
  tft.fillRect(centerX-boxW/2+5, startY-boxH/2+5, boxW-10, boxH-10, BLACK);
  tft.setCursor(centerX-45, startY-12);
  if(onH<10) tft.print("0"); tft.print(onH); tft.print(":");
  if(onM<10) tft.print("0"); tft.print(onM);

  // STOP time box
  tft.fillRect(centerX-boxW/2+5, stopY-boxH/2+5, boxW-10, boxH-10, BLACK);
  tft.setCursor(centerX-45, stopY-12);
  if(offH<10) tft.print("0"); tft.print(offH); tft.print(":");
  if(offM<10) tft.print("0"); tft.print(offM);
}

// For AUTO duration pages: show START and DURATION for whichever slot is active
void showDurationValue(){
  int dur = 0;
  int sh=0, sm=0;

  int *durPtr    = nullptr;
  int *startHptr = nullptr;
  int *startMptr = nullptr;

  if(editAutoSlot == 1){ // Top TIME1
    durPtr    = durationInSeconds ? &durSec_T1  : &durMin_T1;
    startHptr = durationInSeconds ? &onH_T1_Sec : &onH_T1_Min;
    startMptr = durationInSeconds ? &onM_T1_Sec : &onM_T1_Min;
  } else if(editAutoSlot == 2){ // Top TIME2
    durPtr    = durationInSeconds ? &durSec_T2  : &durMin_T2;
    startHptr = durationInSeconds ? &onH_T2_Sec : &onH_T2_Min;
    startMptr = durationInSeconds ? &onM_T2_Sec : &onM_T2_Min;
  } else if(editAutoSlot == 3){ // Bottom TIME1
    durPtr    = durationInSeconds ? &durSec_B1  : &durMin_B1;
    startHptr = durationInSeconds ? &onH_B1_Sec : &onH_B1_Min;
    startMptr = durationInSeconds ? &onM_B1_Sec : &onM_B1_Min;
  } else if(editAutoSlot == 4){ // Bottom TIME2
    durPtr    = durationInSeconds ? &durSec_B2  : &durMin_B2;
    startHptr = durationInSeconds ? &onH_B2_Sec : &onH_B2_Min;
    startMptr = durationInSeconds ? &onM_B2_Sec : &onM_B2_Min;
  } else {
    return;
  }

  sh  = *startHptr;
  sm  = *startMptr;
  dur = *durPtr;

  tft.setTextColor(WHITE);
  tft.setTextSize(3);
  
  // Clear boxes
  tft.fillRect(centerX-boxW/2+5, startY-boxH/2+5, boxW-10, boxH-10, BLACK);
  tft.fillRect(centerX-boxW/2+5, stopY-boxH/2+5, boxW-10, boxH-10, BLACK);

  // START time
  tft.setCursor(centerX-45, startY-12);
  if(sh<10) tft.print("0"); tft.print(sh); tft.print(":");
  if(sm<10) tft.print("0"); tft.print(sm);

  // DURATION value
  tft.setCursor(centerX-50, stopY-12);
  if(dur<10) tft.print("0");
  tft.print(dur);

  if(durationInSeconds){
    tft.print(" SEC");
  } else {
    tft.print(" MIN");
  }
}

void drawTimerPage(){
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.setTextColor(WHITE);
  tft.setTextSize(3);

  tft.setCursor(centerX-45, startY-60); tft.print("START");

  bool isDurationPage = (page==4 || page==5) && (editAutoSlot!=0);

  if(isDurationPage) {
    tft.setCursor(centerX-70, stopY-60); tft.print("DURATION");

    // SEC button visible only in MIN mode
    if(!durationInSeconds){
      tft.fillRect(secBtnX, secBtnY, secBtnW, secBtnH, BLUE);
      tft.setTextColor(WHITE); tft.setTextSize(2);
      tft.setCursor(secBtnX + 10, secBtnY + 13); tft.print("SEC");
    } else {
      tft.fillRect(secBtnX, secBtnY, secBtnW, secBtnH, BLACK);
    }

  } else {
    tft.setCursor(centerX-40, stopY-60); tft.print("STOP");
    tft.fillRect(secBtnX, secBtnY, secBtnW, secBtnH, BLACK);
  }

  // Boxes
  tft.drawRect(centerX-boxW/2, startY-boxH/2, boxW, boxH, WHITE);
  tft.drawRect(centerX-boxW/2, stopY-boxH/2, boxW, boxH, WHITE);

  // +/- buttons
  if(isDurationPage){
    // DURATION row
    drawBtn(leftX,  stopY, "+");
    drawBtn(rightX, stopY, "-");
    // START H/M
    drawBtn(leftX,  startY-50,"+");
    drawBtn(leftX,  startY+50,"-");
    drawBtn(rightX, startY-50,"+");
    drawBtn(rightX, startY+50,"-");
  } else {
    // Legacy 8 buttons (Fan/Light)
    drawBtn(leftX,  startY-50,"+");
    drawBtn(leftX,  startY+50,"-");
    drawBtn(rightX, startY-50,"+");
    drawBtn(rightX, startY+50,"-");

    drawBtn(leftX,  stopY-50,"+");
    drawBtn(leftX,  stopY+50,"-");
    drawBtn(rightX, stopY-50,"+");
    drawBtn(rightX, stopY+50,"-");
  }

  drawHBM_Bar();

  if(isDurationPage){
    showDurationValue();
  } else {
    showTimes();
  }
}

void drawHomePage(){
  setPage(1);
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.drawRect(30, 130, 260, 200, GREEN);
  tft.setTextColor(GREEN);
  tft.setTextSize(7);
  tft.setCursor(centerX - 65, 168); tft.print("JMR");
  tft.setCursor(centerX - 105, 247); tft.print("iFARM");
  tft.fillRect(220, 420, 90, 40, YELLOW);
  tft.setTextColor(BLACK);
  tft.setTextSize(3);
  tft.setCursor(230, 430); tft.print("MENU");
}

void drawMenuPage(){
  setPage(2);
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.setTextColor(WHITE);
  tft.setTextSize(3);

  tft.drawRect(30, 70, 260, 60, WHITE); tft.setCursor(90,90);  tft.print("TOP WATER");
  tft.drawRect(30,150, 260, 60, WHITE); tft.setCursor(60,170); tft.print("BOTTOM WATER");
  tft.drawRect(30,230, 260, 60, WHITE); tft.setCursor(120,250);tft.print("FAN");
  tft.drawRect(30,310, 260, 60, WHITE); tft.setCursor(110,330);tft.print("LIGHT");

  drawHBM_Bar();
}

void showMoistureValue(){
  int raw=analogRead(moisturePin);
  int percent=map(raw,1023,0,0,100);
  tft.fillRect(200,70,80,40,BLACK);
  tft.setCursor(210,85);
  tft.print(percent); tft.print("%");
}

void showCutoffValue(){
  int value = (manualTarget==1)? topCutoff : bottomCutoff;
  tft.fillRect(200,170,80,40,BLACK);
  tft.setCursor(210,170);
  tft.print(value); tft.print("%");
}

void drawMoisturePage(){
  setPage(3);
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.setTextColor(WHITE);
  tft.setTextSize(3);
  tft.setCursor(30,85);  tft.print("MOISTURE:");
  tft.setCursor(65,170); tft.print("CUTOFF:");
  showMoistureValue();
  showCutoffValue();

  drawBtn(centerX - plusMinusOffset,260,"+");
  drawBtn(centerX + plusMinusOffset,260,"-");

  for(int r=btnR;r>btnR-3;r--) tft.drawCircle(centerX, 340, r, WHITE);
  tft.setCursor(centerX-15, 332); tft.print("OK");

  drawHBM_Bar();
}

void drawTopWaterModePage(){
  setPage(7);
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.setTextColor(WHITE);
  tft.setTextSize(3);

  tft.drawRect(30,120,260,80,WHITE); tft.setCursor(120,150); tft.print("AUTO");
  tft.drawRect(30,240,260,80,WHITE); tft.setCursor(100,270); tft.print("MANUAL");

  drawHBM_Bar();
}

void drawTopAutoTimeSelectPage(){
  setPage(9);
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.setTextColor(WHITE); tft.setTextSize(3);
  tft.setCursor(90,100); tft.print("TOP AUTO");
  tft.drawRect(60,150,200,80,WHITE); tft.setCursor(110,180); tft.print("TIME 1");
  tft.drawRect(60,270,200,80,WHITE); tft.setCursor(110,300); tft.print("TIME 2");

  drawHBM_Bar();
}

void drawBottomWaterModePage(){
  setPage(8);
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.setTextColor(WHITE);
  tft.setTextSize(3);

  tft.drawRect(30,120,260,80,WHITE); tft.setCursor(120,150); tft.print("AUTO");
  tft.drawRect(30,240,260,80,WHITE); tft.setCursor(100,270); tft.print("MANUAL");

  drawHBM_Bar();
}

void drawBottomAutoTimeSelectPage(){
  setPage(10);
  tft.fillScreen(BLACK);
  drawRTCtime();
  tft.setTextColor(WHITE); tft.setTextSize(3);
  tft.setCursor(65,100); tft.print("BOTTOM AUTO");
  tft.drawRect(60,150,200,80,WHITE); tft.setCursor(110,180); tft.print("TIME 1");
  tft.drawRect(60,270,200,80,WHITE); tft.setCursor(110,300); tft.print("TIME 2");

  drawHBM_Bar();
}

// Global function to navigate to the previous page
void goBack(){
  int targetPage = previousPage;

  // 1. DURATION EDIT: SEC -> MIN
  if((page==4 || page==5) && (editAutoSlot!=0) && durationInSeconds){
    durationInSeconds = false;
    drawTimerPage();
    return;
  }

  // 2. DURATION EDIT: MIN -> Time Select
  if((page==4 || page==5) && (editAutoSlot!=0)){
    durationInSeconds = false;
    editAutoSlot = 0;
    if(page==5) { drawTopAutoTimeSelectPage(); return; }
    if(page==4) { drawBottomAutoTimeSelectPage(); return; }
  }

  // 3. TIME SELECT -> Mode Page
  if(page==9)  { drawTopWaterModePage();    return; }
  if(page==10) { drawBottomWaterModePage(); return; }

  // 4. MODE PAGE -> Menu
  if(page==7 || page==8) { drawMenuPage(); return; }

  // 5. MOISTURE PAGE -> Mode Page
  if(page==3) {
    if(manualTarget == 1)       { drawTopWaterModePage();    return; }
    else if(manualTarget == 2)  { drawBottomWaterModePage(); return; }
    manualTarget = 0;
  }

  // 6. FAN/LIGHT (editAutoSlot==0) -> Menu
  if((page==5 && editAutoSlot==0) || page==6) {
    drawMenuPage();
    return;
  }

  // Default
  if(targetPage==1)      drawHomePage();
  else if(targetPage==2) drawMenuPage();
  else                   drawMenuPage();
}

/* ---------------- Utility: within START+duration? ---------------- */
bool isWithinStartPlusDuration(int startH, int startM, int durationValue, bool durationIsSeconds, DateTime now){
  if(durationValue <= 0) return false;

  long startTotalSeconds = (long)startH * 3600 + (long)startM * 60;
  long nowTotalSeconds   = (long)now.hour() * 3600 + (long)now.minute() * 60 + now.second();
  long durationSeconds   = durationIsSeconds ? durationValue : (long)durationValue * 60;

  const long MAX_SECONDS = 24L * 3600L;
  long endTotalSeconds = startTotalSeconds + durationSeconds;

  if(endTotalSeconds < MAX_SECONDS){
    return (nowTotalSeconds >= startTotalSeconds && nowTotalSeconds < endTotalSeconds);
  } else {
    long wrappedEndSeconds = endTotalSeconds - MAX_SECONDS;
    return (nowTotalSeconds >= startTotalSeconds) || (nowTotalSeconds < wrappedEndSeconds);
  }
}

/* ---------------- Setup ---------------- */
void setup(){
  Serial.begin(9600);
  Wire.begin();
  rtc.begin();

  uint16_t ID = tft.readID();
  if(ID==0xD3D3) ID=0x9486;
  tft.begin(ID);
  tft.setRotation(0);

  pinMode(relayTop,OUTPUT);
  pinMode(relayBottom,OUTPUT);
  pinMode(relayFan,OUTPUT);
  pinMode(relayLight,OUTPUT);

  digitalWrite(relayTop,HIGH);
  digitalWrite(relayBottom,HIGH);
  digitalWrite(relayFan,HIGH);
  digitalWrite(relayLight,HIGH);

  loadSettings();
  drawHomePage();
}

/* ---------------- Loop ---------------- */
void loop(){
  bool nowTouch = Touch_getXY();

  // ----------- Touch handlers -----------
  if(nowTouch && !lastTouch){

    // Universal HOME & MENU & BACK
    if(pixel_y>btnY && pixel_y<btnY+btnH){
      if(pixel_x>homeX && pixel_x<homeX+btnW){ // HOME
        drawHomePage(); editAutoSlot=0; durationInSeconds = false;
      } else if(pixel_x>backX && pixel_x<backX+btnW){ // BACK
        bool hideBack = (page == 2) || (page == 5 && editAutoSlot==0) || (page == 6);
        if(!hideBack) {
          goBack();
        }
      } else if(pixel_x>menuX && pixel_x<menuX+btnW){ // MENU
        drawMenuPage(); editAutoSlot=0; durationInSeconds = false;
      }
    }

    // Page specific navigation
    else if(page==1 && pixel_x>220 && pixel_x<310 && pixel_y>420){ // Home Menu button
      drawMenuPage();
    } else if(page==2){ // Menu
      if(pixel_y>70 && pixel_y<130)        drawTopWaterModePage();
      else if(pixel_y>150 && pixel_y<210)  drawBottomWaterModePage();
      else if(pixel_y>230 && pixel_y<290){ editingTopAuto = false; editAutoSlot = 0; setPage(5); drawTimerPage(); } // FAN
      else if(pixel_y>310 && pixel_y<370){ editAutoSlot = 0; setPage(6); drawTimerPage(); } // LIGHT
    } else if(page==7){ // Top Water Mode
      if(pixel_y>120 && pixel_y<200){ topWaterAuto = true;  drawTopAutoTimeSelectPage(); }
      else if(pixel_y>240 && pixel_y<320){ topWaterAuto = false; manualTarget = 1; drawMoisturePage(); }
    } else if(page==8){ // Bottom Water Mode
      if(pixel_y>120 && pixel_y<200){ bottomWaterAuto = true;  drawBottomAutoTimeSelectPage(); }
      else if(pixel_y>240 && pixel_y<320){ bottomWaterAuto = false; manualTarget = 2; drawMoisturePage(); }
    } else if(page==9){ // Top AUTO TIME SELECT
      if(pixel_y>150 && pixel_y<230){ editAutoSlot = 1; editingTopAuto = true; durationInSeconds = false; setPage(5); drawTimerPage(); } // TIME1
      else if(pixel_y>270 && pixel_y<350){ editAutoSlot = 2; editingTopAuto = true; durationInSeconds = false; setPage(5); drawTimerPage(); } // TIME2
    } else if(page==10){ // Bottom AUTO TIME SELECT
      if(pixel_y>150 && pixel_y<230){ editAutoSlot = 3; editingTopAuto = false; durationInSeconds = false; setPage(4); drawTimerPage(); } // TIME1
      else if(pixel_y>270 && pixel_y<350){ editAutoSlot = 4; editingTopAuto = false; durationInSeconds = false; setPage(4); drawTimerPage(); } // TIME2
    }
    else if(page==3){ // Moisture page
      int cx=centerX, cy=340;
      if(abs(pixel_x-cx)<btnR && abs(pixel_y-cy)<btnR){ // OK
        if(manualTarget==1)      manualTopOKRequested = true;
        else if(manualTarget==2) manualBottomOKRequested = true;
        saveSettings();
      } else if(abs(pixel_x-(centerX - plusMinusOffset))<btnR && abs(pixel_y-260)<btnR){
        if(manualTarget==1){ topCutoff++;    if(topCutoff>100)    topCutoff=100; }
        else if(manualTarget==2){ bottomCutoff++; if(bottomCutoff>100) bottomCutoff=100; }
        showCutoffValue(); saveSettings();
      } else if(abs(pixel_x-(centerX + plusMinusOffset))<btnR && abs(pixel_y-260)<btnR){
        if(manualTarget==1){ topCutoff--;    if(topCutoff<0)      topCutoff=0; }
        else if(manualTarget==2){ bottomCutoff--; if(bottomCutoff<0)   bottomCutoff=0; }
        showCutoffValue(); saveSettings();
      }
    }
    else if((page==4 || page==5) && (editAutoSlot!=0)){ // Duration timer pages

      // SEC Button
      if(!durationInSeconds && pixel_x > secBtnX && pixel_x < secBtnX + secBtnW &&
         pixel_y > secBtnY && pixel_y < secBtnY + secBtnH) {
        durationInSeconds = true;
        drawTimerPage();
        return;
      }

      int *durPtr    = nullptr;
      int *startHptr = nullptr;
      int *startMptr = nullptr;

      if(editAutoSlot==1){
        durPtr    = durationInSeconds ? &durSec_T1  : &durMin_T1;
        startHptr = durationInSeconds ? &onH_T1_Sec : &onH_T1_Min;
        startMptr = durationInSeconds ? &onM_T1_Sec : &onM_T1_Min;
      } else if(editAutoSlot==2){
        durPtr    = durationInSeconds ? &durSec_T2  : &durMin_T2;
        startHptr = durationInSeconds ? &onH_T2_Sec : &onH_T2_Min;
        startMptr = durationInSeconds ? &onM_T2_Sec : &onM_T2_Min;
      } else if(editAutoSlot==3){ 
        durPtr    = durationInSeconds ? &durSec_B1  : &durMin_B1;
        startHptr = durationInSeconds ? &onH_B1_Sec : &onH_B1_Min;
        startMptr = durationInSeconds ? &onM_B1_Sec : &onM_B1_Min;
      } else if(editAutoSlot==4){
        durPtr    = durationInSeconds ? &durSec_B2  : &durMin_B2;
        startHptr = durationInSeconds ? &onH_B2_Sec : &onH_B2_Min;
        startMptr = durationInSeconds ? &onM_B2_Sec : &onM_B2_Min;
      }

      if (startHptr == nullptr) return;

      // START H/M
      if(abs(pixel_x-leftX)<btnR && abs(pixel_y-(startY-50))<btnR){ // H +
        (*startHptr)++; if(*startHptr>23)*startHptr=0; showDurationValue(); saveSettings();
      } else if(abs(pixel_x-leftX)<btnR && abs(pixel_y-(startY+50))<btnR){ // H -
        (*startHptr)--; if(*startHptr<0)*startHptr=23; showDurationValue(); saveSettings();
      } else if(abs(pixel_x-rightX)<btnR && abs(pixel_y-(startY-50))<btnR){ // M +
        (*startMptr)++; if(*startMptr>59)*startMptr=0; showDurationValue(); saveSettings();
      } else if(abs(pixel_x-rightX)<btnR && abs(pixel_y-(startY+50))<btnR){ // M -
        (*startMptr)--; if(*startMptr<0)*startMptr=59; showDurationValue(); saveSettings();
      }

      // DURATION
      else if(abs(pixel_x-leftX)<btnR && abs(pixel_y-stopY)<btnR){ // +
        int maxDur = 240;
        (*durPtr)++; if(*durPtr>maxDur)*durPtr=maxDur; showDurationValue(); saveSettings();
      } else if(abs(pixel_x-rightX)<btnR && abs(pixel_y-stopY)<btnR){ // -
        (*durPtr)--; if(*durPtr<0)*durPtr=0; showDurationValue(); saveSettings();
      }
    }
    else if(page==5 || page==6){ // Fan or Light legacy timer
      int *onH, *onM, *offH, *offM;

      if(page==5){ onH=&onH3; onM=&onM3; offH=&offH3; offM=&offM3; } // FAN
      else { onH=&onH4; onM=&onM4; offH=&offH4; offM=&offM4; }       // LIGHT

      if(abs(pixel_x-leftX)<btnR && abs(pixel_y-(startY-50))<btnR){ (*onH)++; if(*onH>23)*onH=0; showTimes(); saveSettings(); }
      else if(abs(pixel_x-leftX)<btnR && abs(pixel_y-(startY+50))<btnR){ (*onH)--; if(*onH<0)*onH=23; showTimes(); saveSettings(); }
      else if(abs(pixel_x-rightX)<btnR && abs(pixel_y-(startY-50))<btnR){ (*onM)++; if(*onM>59)*onM=0; showTimes(); saveSettings(); }
      else if(abs(pixel_x-rightX)<btnR && abs(pixel_y-(startY+50))<btnR){ (*onM)--; if(*onM<0)*onM=59; showTimes(); saveSettings(); }
      else if(abs(pixel_x-leftX)<btnR && abs(pixel_y-(stopY-50))<btnR){ (*offH)++; if(*offH>23)*offH=0; showTimes(); saveSettings(); }
      else if(abs(pixel_x-leftX)<btnR && abs(pixel_y-(stopY+50))<btnR){ (*offH)--; if(*offH<0)*offH=23; showTimes(); saveSettings(); }
      else if(abs(pixel_x-rightX)<btnR && abs(pixel_y-(stopY-50))<btnR){ (*offM)++; if(*offM>59)*offM=0; showTimes(); saveSettings(); }
      else if(abs(pixel_x-rightX)<btnR && abs(pixel_y-(stopY-50))<btnR){ (*offM)--; if(*offM<0)*offM=59; showTimes(); saveSettings(); }
    }
  }
  lastTouch = nowTouch;

  // ----------- Backend Control -----------

  DateTime now = rtc.now();

  // Read moisture for MANUAL logic
  int raw = analogRead(moisturePin);
  int percent = map(raw,1023,0,0,100);

  // live moisture on page 3
  if(page==3){
    showMoistureValue();
  }

  bool topActive    = false;
  bool bottomActive = false;

  // --- Top Water Control ---
  if(topWaterAuto){
    bool t1_min_active = isWithinStartPlusDuration(onH_T1_Min, onM_T1_Min, durMin_T1, false, now);
    bool t1_sec_active = isWithinStartPlusDuration(onH_T1_Sec, onM_T1_Sec, durSec_T1, true,  now);
    bool t1_active     = t1_min_active || t1_sec_active;

    bool t2_min_active = isWithinStartPlusDuration(onH_T2_Min, onM_T2_Min, durMin_T2, false, now);
    bool t2_sec_active = isWithinStartPlusDuration(onH_T2_Sec, onM_T2_Sec, durSec_T2, true,  now);
    bool t2_active     = t2_min_active || t2_sec_active;

    topActive = t1_active || t2_active;

  } else {
    if (manualTopOKRequested) {
      if (percent < topCutoff) { topActive = true; }
      else { topActive = false; manualTopOKRequested = false; }
    } else { topActive = false; }
  }

  // --- Bottom Water Control ---
  if(bottomWaterAuto){
    bool b1_min_active = isWithinStartPlusDuration(onH_B1_Min, onM_B1_Min, durMin_B1, false, now);
    bool b1_sec_active = isWithinStartPlusDuration(onH_B1_Sec, onM_B1_Sec, durSec_B1, true,  now);
    bool b1_active     = b1_min_active || b1_sec_active;

    bool b2_min_active = isWithinStartPlusDuration(onH_B2_Min, onM_B2_Min, durMin_B2, false, now);
    bool b2_sec_active = isWithinStartPlusDuration(onH_B2_Sec, onM_B2_Sec, durSec_B2, true,  now);
    bool b2_active     = b2_min_active || b2_sec_active;

    bottomActive = b1_active || b2_active;

  } else {
    if (manualBottomOKRequested) {
      if (percent < bottomCutoff) { bottomActive = true; }
      else { bottomActive = false; manualBottomOKRequested = false; }
    } else { bottomActive = false; }
  }

  // --- Fan timer ---
  bool fan_on = (now.hour() >  onH3 || (now.hour()==onH3 && now.minute()>=onM3)) &&
                (now.hour() < offH3 || (now.hour()==offH3 && now.minute() <offM3));
  digitalWrite(relayFan, fan_on ? LOW : HIGH);

  // --- Light timer ---
  bool light_on = (now.hour() >  onH4 || (now.hour()==onH4 && now.minute()>=onM4)) &&
                  (now.hour() < offH4 || (now.hour()==offH4 && now.minute() <offM4));
  digitalWrite(relayLight, light_on ? LOW : HIGH);

  // --- Relays ---
  digitalWrite(relayTop,    (topActive || bottomActive) ? LOW : HIGH);
  digitalWrite(relayBottom, bottomActive ? LOW : HIGH);

  // Update RTC display every second
  if(millis() - lastRTCupdate > 1000){
    drawRTCtime();
    lastRTCupdate = millis();
  }
}
