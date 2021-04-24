#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <WiredDevice.h>
#include <RegisterBasedWiredDevice.h>
#include <Accelerometer.h>
#include <AccelerometerMMA8451.h>

#ifdef ESP8266
#include <ESP8266WiFi.h>

#define BUZZER_PIN D8
#define DISPLAY_CS_PIN D3
#define DISPLAY_DC_PIN D0
#define DISPLAY_RS_PIN D4
#endif

#ifdef __AVR__
#include "LowPower.h"

#define BUZZER_PIN 6
#define DISPLAY_CS_PIN 10
#define DISPLAY_DC_PIN 9
#define DISPLAY_RS_PIN 8
#endif

/**
 * Depending on how the sensor is oriented in relation to the display, we need to adjust sensor readings.
 * Uncomment only one of ORN_X_ and ORN_Y_ so that the X and Y are read from correct sensor direction:
 */
//#define ORN_X_FROM_X
#define ORN_X_FROM_Y
//#define ORN_X_FROM_Z
#define ORN_Y_FROM_X
//#define ORN_Y_FROM_Y
//#define ORN_Y_FROM_Z
/**
 * Depending on how the sensor is oriented, the reading needs to be inverted or not. Uncomment if any of
 * X or Y reading needs to be inverted:
 */
#define ORN_X_INV
//#define ORN_Y_INV

#define LIVES 3
#define BALLSIZE 4
#define BALL_RADIUS BALLSIZE/2
#define DELAY 50
#define BATONWIDTH 3
#define BATON_SPEED_FACTOR 10.0
#define BALL_SPEED_FACTOR 3.0
#define BRICK_SPACING 2
#define BRICK_COLS 5
#define BRICK_ROWS 3

U8G2_PCD8544_84X48_F_4W_HW_SPI u8g2(U8G2_R0, DISPLAY_CS_PIN, DISPLAY_DC_PIN, DISPLAY_RS_PIN);
AccelerometerMMA8451 acc(0);

struct fpoint {
  float x;
  float y;
};

struct upoint {
  uint8_t x;
  uint8_t y;
};

uint8_t max_x, max_y, points;
struct fpoint ball, speed = {0.0, 0.0};
uint8_t batonLength = 20, brickHeight, brickWidth;
float batonX;
char bricks[BRICK_ROWS][BRICK_COLS];

uint16_t tonesFlag[][2] = {{698, 1}, {880, 1}, {1047, 1}, {0, 0}};
uint16_t tonesLevel[][2] = {{1047, 1}, {988, 1}, {1047, 1}, {988, 1}, {1047, 1}, {0, 0}};
uint16_t tonesSad[][2] = {{262, 1}, {247, 1}, {233, 1}, {220, 3}, {0, 0}};
uint16_t tonesHit[][2] = {{698, 1}, {0, 0}};
uint16_t tonesBaton[][2] = {{262, 1}, {0, 0}};

uint8_t melodyIndex;
uint16_t (*currentMelody)[2];

void startMotionDetection() {
  acc.standby();
  acc.setMotionDetection(false, true, 0x03);
  acc.setMotionDetectionThreshold(false, 0x1a);
  acc.setMotionDetectionCount(0x10);
  acc.enableInterrupt(AccelerometerMMA8451::INT_FF_MT);
  acc.routeInterruptToInt1(AccelerometerMMA8451::INT_FF_MT);
  acc.activate();
}

void setupMMA()
{
  acc.standby();
  acc.disableInterrupt(AccelerometerMMA8451::INT_ALL);
  acc.setDynamicRange(AccelerometerMMA8451::DR_2G);
  acc.setOutputDataRate(AccelerometerMMA8451::ODR_50HZ_20_MS);
  acc.activate();
}

void drawGame(void) {
  static char buf[12];
  u8g2.clearBuffer();
  // draw marble
  u8g2.drawDisc(ball.x, ball.y, BALLSIZE/2);
  // draw baton
  u8g2.drawBox(batonX - batonLength / 2, 0, batonLength, BATONWIDTH);
  // draw bricks

  for (short row = 0; row < BRICK_ROWS; row++) {
    for (short col = 0; col < BRICK_COLS; col++) {
      if (bricks[row][col] > 0) {
        u8g2.drawFrame(col * (brickWidth + BRICK_SPACING), max_y - (row * (brickHeight + BRICK_SPACING)) - brickHeight, brickWidth, brickHeight);
      }
    }
  }

  // write points and time
  itoa(points, buf, 10);
  u8g2.drawStr(0, 5, buf);
//  itoa(timer/10, buf, 10);
//  uint8_t width = u8g2.getStrWidth(buf);
//  u8g2.drawStr(max_x-width, 5, buf);
  u8g2.sendBuffer();
}

void showPopup(char *line_1, char *line_2) {
  u8g2.clearBuffer();
  u8g2.drawRFrame(0, 0, max_x, max_y, 7);
  uint8_t width = u8g2.getStrWidth(line_1);
  u8g2.drawStr((max_x-width)/2, max_y/2 - 2, line_1);
  width = u8g2.getStrWidth(line_2);
  u8g2.drawStr((max_x-width)/2, max_y/2 + 8, line_2);
  u8g2.sendBuffer();
}

void playMelody(uint16_t (*melody)[2]) {
  currentMelody = melody;
  melodyIndex = 0;
}

// used to play the melody asynchronously while the user is playing
void playSound(void) {
  if (currentMelody) {
    uint8_t totalCount = 0;
    for (uint8_t i = 0; 1; i++) {
      uint16_t freq = currentMelody[i][0];
      uint16_t dur = currentMelody[i][1];
      if (melodyIndex == totalCount) {
        if (dur == 0) {
          noTone(BUZZER_PIN);
          currentMelody = NULL;
          melodyIndex = 0;
        } else {
          tone(BUZZER_PIN, freq);
        }
      }
      totalCount += dur;
      if (totalCount > melodyIndex)
        break;
    }
    melodyIndex++;
  }
}

// bool isCollided(struct upoint point) {
//   return abs(ball.x-point.x) < 3 && abs(ball.y-point.y) < 3;
// }

void calculateYSpeed() {
  speed.y = sqrt(1.0 - speed.x * speed.x);  
}

void batonCollision() {
  if (speed.y > 0) return; // skip if ball is heading up
  if (ball.y > BATONWIDTH + BALLSIZE / 2) return; // skip if ball is above the baton
  float batonLeftX = batonX - batonLength / 2 - BALLSIZE / 2;
  float batonRightX = batonX + batonLength / 2 + BALLSIZE / 2;
  if (ball.x < batonLeftX || ball.x > batonRightX) return; // skip if ball outside baton reach
  float extendedBatonLength = batonRightX - batonLeftX;
  speed.x = (ball.x - batonLeftX - extendedBatonLength / 2) / extendedBatonLength * 2;
  calculateYSpeed();
  playMelody(tonesBaton);
}

void wallCollision() {
  if (speed.x < 0) {
    if (ball.x < BALLSIZE / 2) speed.x = -speed.x;
  } else {
    if (ball.x > max_x - BALLSIZE / 2) speed.x = -speed.x;
  }
  if (speed.y > 0 && ball.y > max_y - BALLSIZE / 2) speed.y = -speed.y;
}

void brickCollision() {
  // u8g2.drawFrame(col * (brickWidth + BRICK_SPACING), max_y - (row * (brickHeight + BRICK_SPACING)) - brickHeight, brickWidth, brickHeight);
  for (short row = 0; row < BRICK_ROWS; row++) {
    short brickTop = max_y - (row * (brickHeight + BRICK_SPACING)) - brickHeight;
    short brickBottom = brickTop + brickHeight;
    if (ball.y < brickTop - BALL_RADIUS || ball.y > brickBottom + BALL_RADIUS) continue;
    for (short col = 0; col < BRICK_COLS; col++) {
      if (bricks[row][col] == 0) continue;
      short brickLeft = col * (brickWidth + BRICK_SPACING);
      short brickRight = brickLeft + brickWidth;
      if (ball.x < brickLeft - BALL_RADIUS || ball.x > brickRight + BALL_RADIUS) continue;
      // hit the brick!
      bricks[row][col] = 0;
      points++;
      if (ball.x >= brickLeft && ball.x <= brickRight) {
        speed.y = -speed.y;
      } else {
        speed.x = -speed.x;
      }
      playMelody(tonesHit);
      return;
    }
  }
}

void melodySync(uint16_t (*melody)[2]) {
  // this is played synchronously
  for (uint8_t i = 0; melody[i][1] > 0; i++) {
    tone(BUZZER_PIN, melody[i][0], melody[i][1]*300);
    delay(melody[i][1] * 300 + 50);
  }
}

void initGame() {
  points = 0;
  for (short row = 0; row < BRICK_ROWS; row++) {
    for (short col = 0; col < BRICK_COLS; col++) {
      bricks[row][col] = 1;
    }
  }
  batonX = max_x / 2.0;
  ball.x = max_x / 2.0;
  ball.y = BATONWIDTH + BALLSIZE / 2;
  speed.x = random(100) / 100.0 - 0.5;
  calculateYSpeed();
}

void gameOver(void) {
  char msg[50];
  sprintf(msg, "score: %d", points);
  showPopup("GAME OVER", msg);
  melodySync(tonesSad);
  initGame();
}

void gameEnd(void) {
  showPopup("You won", "Good job!");
  melodySync(tonesLevel);
}

void updateMovement(void) {
  ball.x += speed.x * BALL_SPEED_FACTOR;
  ball.y += speed.y * BALL_SPEED_FACTOR;

  #ifdef ORN_X_FROM_X
  float xg = acc.readXg();
  #endif
  #ifdef ORN_X_FROM_Y
  float xg = acc.readYg();
  #endif
  #ifdef ORN_X_FROM_Z
  float xg = acc.readZg();
  #endif
  #ifdef ORN_Y_FROM_X
  float yg = acc.readXg();
  #endif
  #ifdef ORN_Y_FROM_Y
  float yg = acc.readYg();
  #endif
  #ifdef ORN_Y_FROM_Z
  float yg = acc.readZg();
  #endif
  #ifdef ORN_X_INV
  xg = -xg;
  #endif
  #ifdef ORN_Y_INV
  yg = -yg;
  #endif

  float newBatonX = batonX + xg * BATON_SPEED_FACTOR;
  if (newBatonX - batonLength / 2 > 0 && newBatonX + batonLength / 2 < max_x) batonX = newBatonX;
}

void goToSleep() {
  showPopup("SLEEPING...", "shake to wake");
  startMotionDetection();
#ifdef ESP8266
  ESP.deepSleep(0);
#endif
#ifdef __AVR__
LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
#endif
}

void setup(void) {
  randomSeed(analogRead(0));
  Serial.begin(115200);
#ifdef ESP8266
  WiFi.mode(WIFI_OFF);
#endif

  setupMMA();

  u8g2.begin();
  u8g2.setFont(u8g2_font_baby_tf);
  max_x = u8g2.getDisplayWidth();
  max_y = u8g2.getDisplayHeight();
  brickWidth = (max_x + BRICK_SPACING) / BRICK_COLS - BRICK_SPACING;
  brickHeight = max_y / 2 / BRICK_ROWS - BRICK_SPACING;

  initGame();
}

void loop(void) {
  if (ball.y < 0) gameOver();
  if (points == BRICK_ROWS * BRICK_COLS) gameEnd();
  batonCollision();
  brickCollision();
  wallCollision();
  drawGame();
  updateMovement();

  playSound();
  delay(DELAY);
}
