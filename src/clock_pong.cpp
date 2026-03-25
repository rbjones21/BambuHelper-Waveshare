/*
 * Pong/Breakout animated clock for BambuHelper
 * Adapted for 800x480 LovyanGFX display.
 */

#include "clock_pong.h"
#include "config.h"
#include "settings.h"
#include "display_ui.h"
#include "lgfx_waveshare.h"
#include <time.h>

extern LGFX_Waveshare43 tft;

// ========== Constants (scaled for 800x480) ==========
#define ARK_BRICK_ROWS    5
#define ARK_BRICK_COLS    16
#define ARK_BRICK_W       46
#define ARK_BRICK_H       12
#define ARK_BRICK_GAP     3
#define ARK_BRICK_START_X 8
#define ARK_BRICK_START_Y 50
#define ARK_BALL_SIZE     6
#define ARK_PADDLE_Y      460
#define ARK_PADDLE_H      6
#define ARK_PADDLE_W      60
#define ARK_TIME_Y        220
#define ARK_DATE_Y        14
#define ARK_BALL_SPEED    4.5f
#define ARK_PADDLE_SPEED  6
#define ARK_UPDATE_MS     20
#define ARK_MAX_FRAGS     20
#define ARK_TIME_OVERRIDE_MS 60000

// Font 7 digit dimensions at size 2 (~96px tall)
#define DIGIT_W  64
#define DIGIT_H  96
#define COLON_W  24

static const uint16_t brickColors[ARK_BRICK_ROWS] = {
  0xF800, 0xFD20, 0xFFE0, 0x07E0, 0x001F,
};

struct PongFragment {
  float x, y, vx, vy;
  bool active;
};

// ========== State ==========
static bool arkBricks[ARK_BRICK_ROWS][ARK_BRICK_COLS];
static int arkBrickCount = 0;

static float ballX, ballY, ballVX, ballVY;
static bool ballActive = false;
static int prevBallX = -1, prevBallY = -1;

static int paddleX = SCREEN_W / 2, prevPaddleX = SCREEN_W / 2;

static bool initialized = false;
static unsigned long lastUpdateMs = 0;
static int lastMinute = -1;
static bool animTriggered = false;

static int dispHour = 0, dispMin = 0;
static bool timeOverridden = false;
static unsigned long timeOverrideStart = 0;

static int targetDigits[4], targetValues[4];
static int numTargets = 0, currentTarget = 0;
static bool breaking = false;

static PongFragment frags[ARK_MAX_FRAGS];
static int fragTimer = 0;

static float digitOffsetY[5] = {0};
static float digitVelocity[5] = {0};
static int prevDigitY[5] = {0};

static char prevDigits[6] = {0};
static bool prevColon = false;
static char prevDateStr[20] = {0};

// ========== Digit bounce ==========
static void triggerBounce(int i) {
  if (i >= 0 && i < 5) digitVelocity[i] = -8.0f;
}

static void updateBounce() {
  static unsigned long lastPhys = 0;
  unsigned long now = millis();
  float dt = (now - lastPhys) / 1000.0f;
  if (dt > 0.1f || lastPhys == 0) dt = 0.025f;
  lastPhys = now;
  float scale = dt / 0.05f;

  for (int i = 0; i < 5; i++) {
    if (digitOffsetY[i] != 0 || digitVelocity[i] != 0) {
      digitVelocity[i] += 3.0f * scale;
      digitOffsetY[i] += digitVelocity[i] * scale;
      if (digitOffsetY[i] >= 0) { digitOffsetY[i] = 0; digitVelocity[i] = 0; }
    }
  }
}

static bool showColon() { return (millis() % 1000) < 500; }

// ========== Bricks ==========
static void initBricks() {
  arkBrickCount = 0;
  for (int r = 0; r < ARK_BRICK_ROWS; r++)
    for (int c = 0; c < ARK_BRICK_COLS; c++) {
      arkBricks[r][c] = true;
      arkBrickCount++;
    }
}

static void drawBrick(int r, int c) {
  int x = ARK_BRICK_START_X + c * (ARK_BRICK_W + ARK_BRICK_GAP);
  int y = ARK_BRICK_START_Y + r * (ARK_BRICK_H + ARK_BRICK_GAP);
  tft.fillRect(x, y, ARK_BRICK_W, ARK_BRICK_H, brickColors[r]);
  tft.drawFastHLine(x, y, ARK_BRICK_W, TFT_WHITE);
  tft.drawFastHLine(x, y + ARK_BRICK_H - 1, ARK_BRICK_W, TFT_BLACK);
}

static void drawAllBricks() {
  for (int r = 0; r < ARK_BRICK_ROWS; r++)
    for (int c = 0; c < ARK_BRICK_COLS; c++)
      if (arkBricks[r][c]) drawBrick(r, c);
}

static void clearBrick(int r, int c) {
  int x = ARK_BRICK_START_X + c * (ARK_BRICK_W + ARK_BRICK_GAP);
  int y = ARK_BRICK_START_Y + r * (ARK_BRICK_H + ARK_BRICK_GAP);
  tft.fillRect(x, y, ARK_BRICK_W, ARK_BRICK_H, TFT_BLACK);
}

// ========== Ball ==========
static void spawnBall() {
  ballX = paddleX;
  ballY = ARK_PADDLE_Y - ARK_BALL_SIZE - 2;
  float angle = random(40, 140) * PI / 180.0f;
  ballVX = ARK_BALL_SPEED * cos(angle);
  ballVY = -ARK_BALL_SPEED * sin(angle);
  if (fabsf(ballVX) < 1.5f) ballVX = (ballVX >= 0) ? 1.5f : -1.5f;
  if (fabsf(ballVY) < 1.5f) ballVY = -2.5f;
  ballActive = true;
}

static int digitX(int i);

static void markBallDamage(int bx, int by) {
  int bx2 = bx + ARK_BALL_SIZE;
  int by2 = by + ARK_BALL_SIZE;
  for (int i = 0; i < 5; i++) {
    int dx = digitX(i);
    int dw = (i == 2) ? COLON_W : DIGIT_W + 4;
    int dy = (i == 2) ? ARK_TIME_Y : (prevDigitY[i] ? prevDigitY[i] : ARK_TIME_Y);
    if (bx2 > dx && bx < dx + dw && by2 > dy && by < dy + DIGIT_H) {
      if (i == 2) prevColon = !showColon();
      else prevDigits[i] = 0;
    }
  }
  if (by2 > ARK_DATE_Y && by < ARK_DATE_Y + 20) {
    prevDateStr[0] = '\0';
  }
}

static bool overlapsText(int bx, int by) {
  int bx2 = bx + ARK_BALL_SIZE;
  int by2 = by + ARK_BALL_SIZE;
  if (by2 > ARK_TIME_Y && by < ARK_TIME_Y + DIGIT_H) {
    int tLeft = digitX(0) - 4;
    int tRight = digitX(4) + DIGIT_W + 8;
    if (bx2 > tLeft && bx < tRight) return true;
  }
  if (by2 > ARK_DATE_Y && by < ARK_DATE_Y + 20) {
    if (bx2 > 200 && bx < 600) return true;
  }
  return false;
}

static void drawBall() {
  if (prevBallX >= 0) {
    if (!overlapsText(prevBallX, prevBallY)) {
      tft.fillRect(prevBallX, prevBallY, ARK_BALL_SIZE, ARK_BALL_SIZE, TFT_BLACK);
    }
  }
  if (!overlapsText((int)ballX, (int)ballY)) {
    tft.fillRect((int)ballX, (int)ballY, ARK_BALL_SIZE, ARK_BALL_SIZE, TFT_WHITE);
  }
  prevBallX = (int)ballX;
  prevBallY = (int)ballY;
}

// ========== Paddle ==========
static void drawPaddle() {
  if (prevPaddleX != paddleX)
    tft.fillRect(prevPaddleX - ARK_PADDLE_W / 2, ARK_PADDLE_Y, ARK_PADDLE_W, ARK_PADDLE_H, TFT_BLACK);
  tft.fillRect(paddleX - ARK_PADDLE_W / 2, ARK_PADDLE_Y, ARK_PADDLE_W, ARK_PADDLE_H, TFT_CYAN);
  tft.drawFastHLine(paddleX - ARK_PADDLE_W / 2, ARK_PADDLE_Y, ARK_PADDLE_W, TFT_WHITE);
  prevPaddleX = paddleX;
}

static void updatePaddle() {
  int target;
  if (ballActive && ballVY > 0) {
    target = (int)ballX;
  } else {
    target = SCREEN_W / 2;
  }
  if (paddleX < target - 3) paddleX += ARK_PADDLE_SPEED;
  else if (paddleX > target + 3) paddleX -= ARK_PADDLE_SPEED;
  if (paddleX < ARK_PADDLE_W / 2) paddleX = ARK_PADDLE_W / 2;
  if (paddleX > SCREEN_W - ARK_PADDLE_W / 2) paddleX = SCREEN_W - ARK_PADDLE_W / 2;
}

// ========== Brick collision ==========
static bool checkBrickCollision() {
  int bx = (int)ballX, by = (int)ballY;
  for (int r = 0; r < ARK_BRICK_ROWS; r++) {
    for (int c = 0; c < ARK_BRICK_COLS; c++) {
      if (!arkBricks[r][c]) continue;
      int rx = ARK_BRICK_START_X + c * (ARK_BRICK_W + ARK_BRICK_GAP);
      int ry = ARK_BRICK_START_Y + r * (ARK_BRICK_H + ARK_BRICK_GAP);
      if (bx + ARK_BALL_SIZE > rx && bx < rx + ARK_BRICK_W &&
          by + ARK_BALL_SIZE > ry && by < ry + ARK_BRICK_H) {
        arkBricks[r][c] = false;
        arkBrickCount--;
        clearBrick(r, c);
        float oL = (bx + ARK_BALL_SIZE) - rx, oR = (rx + ARK_BRICK_W) - bx;
        float oT = (by + ARK_BALL_SIZE) - ry, oB = (ry + ARK_BRICK_H) - by;
        if (min(oL, oR) < min(oT, oB)) ballVX = -ballVX; else ballVY = -ballVY;
        return true;
      }
    }
  }
  return false;
}

// ========== Ball physics ==========
static void updateBallPhysics() {
  if (!ballActive) return;
  ballX += ballVX;
  ballY += ballVY;

  if (ballX <= 0) { ballX = 0; ballVX = fabsf(ballVX); }
  if (ballX >= SCREEN_W - ARK_BALL_SIZE) { ballX = SCREEN_W - ARK_BALL_SIZE; ballVX = -fabsf(ballVX); }
  if (ballY <= 0) { ballY = 0; ballVY = fabsf(ballVY); }

  if (ballVY > 0 && ballY + ARK_BALL_SIZE >= ARK_PADDLE_Y && ballY < ARK_PADDLE_Y + ARK_PADDLE_H) {
    int pL = paddleX - ARK_PADDLE_W / 2, pR = paddleX + ARK_PADDLE_W / 2;
    if (ballX + ARK_BALL_SIZE >= pL && ballX <= pR) {
      ballY = ARK_PADDLE_Y - ARK_BALL_SIZE;
      float hit = (ballX + ARK_BALL_SIZE / 2.0f - pL) / (float)ARK_PADDLE_W;
      float angle = (150.0f - hit * 120.0f) * PI / 180.0f;
      ballVX = ARK_BALL_SPEED * cos(angle);
      ballVY = -ARK_BALL_SPEED * sin(angle);
      if (fabsf(ballVX) < 1.5f) ballVX = (ballVX >= 0) ? 1.5f : -1.5f;
      if (fabsf(ballVY) < 1.5f) ballVY = -2.0f;
    }
  }

  if (ballY > SCREEN_H) spawnBall();
  checkBrickCollision();
  if (arkBrickCount <= 0) { initBricks(); drawAllBricks(); }
}

// ========== Fragments ==========
static void spawnDigitFragments(int dx, int dy) {
  for (int i = 0; i < ARK_MAX_FRAGS; i++) {
    frags[i].active = true;
    frags[i].x = dx + random(0, DIGIT_W);
    frags[i].y = dy + random(0, DIGIT_H);
    frags[i].vx = random(-30, 30) / 10.0f;
    frags[i].vy = random(-40, -10) / 10.0f;
  }
  fragTimer = 30;
}

static void updateFragments() {
  if (fragTimer <= 0) return;
  fragTimer--;
  for (int i = 0; i < ARK_MAX_FRAGS; i++) {
    if (!frags[i].active) continue;
    tft.fillRect((int)frags[i].x, (int)frags[i].y, 4, 4, TFT_BLACK);
    frags[i].x += frags[i].vx;
    frags[i].y += frags[i].vy;
    frags[i].vy += 0.3f;
    if (frags[i].y > SCREEN_H || frags[i].x < -10 || frags[i].x > SCREEN_W + 10) {
      frags[i].active = false;
      continue;
    }
    tft.fillRect((int)frags[i].x, (int)frags[i].y, 4, 4, brickColors[random(0, ARK_BRICK_ROWS)]);
  }
  if (fragTimer == 0) {
    for (int i = 0; i < ARK_MAX_FRAGS; i++) {
      if (frags[i].active) {
        tft.fillRect((int)frags[i].x, (int)frags[i].y, 4, 4, TFT_BLACK);
        frags[i].active = false;
      }
    }
  }
}

// ========== Digit targets ==========
static void calcTargets(int hour, int mn) {
  numTargets = 0;
  int newMin = mn + 1, newHour = hour;
  if (newMin >= 60) { newMin = 0; newHour = (newHour + 1) % 24; }
  int oldD[4] = {hour / 10, hour % 10, mn / 10, mn % 10};
  int newD[4] = {newHour / 10, newHour % 10, newMin / 10, newMin % 10};
  int map[4] = {0, 1, 3, 4};
  for (int i = 3; i >= 0; i--) {
    if (oldD[i] != newD[i]) {
      targetDigits[numTargets] = map[i];
      targetValues[numTargets] = newD[i];
      numTargets++;
    }
  }
}

static void applyDigitValue(int di, int dv) {
  int ht = dispHour / 10, ho = dispHour % 10;
  int mt = dispMin / 10, mo = dispMin % 10;
  if (di == 0) { ht = dv; dispHour = ht * 10 + ho; }
  else if (di == 1) { ho = dv; dispHour = ht * 10 + ho; }
  else if (di == 3) { mt = dv; dispMin = mt * 10 + mo; }
  else if (di == 4) { mo = dv; dispMin = mt * 10 + mo; }
}

// ========== Draw time ==========
#define TIME_TOTAL_W  (4 * DIGIT_W + COLON_W)
#define TIME_START_X  ((SCREEN_W - TIME_TOTAL_W) / 2)

static int digitX(int i) {
  if (i < 2) return TIME_START_X + i * DIGIT_W;
  if (i == 2) return TIME_START_X + 2 * DIGIT_W;
  return TIME_START_X + 2 * DIGIT_W + COLON_W + (i - 3) * DIGIT_W;
}

static void drawTime() {
  tft.setTextSize(2);
  char digits[5];
  if (netSettings.use24h) {
    digits[0] = '0' + (dispHour / 10);
    digits[1] = '0' + (dispHour % 10);
  } else {
    int h = dispHour % 12;
    if (h == 0) h = 12;
    digits[0] = (h >= 10) ? '1' : ' ';
    digits[1] = '0' + (h % 10);
  }
  digits[2] = ':';
  digits[3] = '0' + (dispMin / 10);
  digits[4] = '0' + (dispMin % 10);

  bool colon = showColon();

  tft.setFont(&fonts::Font7);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  for (int i = 0; i < 5; i++) {
    if (i == 2) continue;

    int x = digitX(i);
    int y = ARK_TIME_Y + (int)digitOffsetY[i];
    bool bouncing = (digitOffsetY[i] != 0 || digitVelocity[i] != 0);
    bool changed = (digits[i] != prevDigits[i]) || bouncing || (prevDigitY[i] != y);

    if (changed) {
      int clearW = DIGIT_W + 4;
      if (prevDigitY[i] != 0)
        tft.fillRect(x, prevDigitY[i], clearW, DIGIT_H, TFT_BLACK);
      if (prevDigitY[i] != y)
        tft.fillRect(x, y, clearW, DIGIT_H, TFT_BLACK);

      tft.setFont(&fonts::Font7);
      tft.setTextSize(2);
      tft.setCursor(x, y);
      tft.print(digits[i]);
      prevDigits[i] = digits[i];
      prevDigitY[i] = y;
    }
  }

  // Colon
  if (colon != prevColon) {
    int cx = digitX(2);
    tft.fillRect(cx, ARK_TIME_Y, COLON_W, DIGIT_H, TFT_BLACK);
    if (colon) {
      tft.setFont(&fonts::Font7);
      tft.setTextSize(2);
      tft.setCursor(cx, ARK_TIME_Y);
      tft.print(':');
    }
    prevColon = colon;
  }

  // AM/PM
  if (!netSettings.use24h) {
    tft.setFont(FONT_BODY);
    tft.setTextSize(1);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    int ampmX = digitX(4) + DIGIT_W + 4;
    tft.setCursor(ampmX, ARK_TIME_Y + DIGIT_H - 24);
    tft.print(dispHour < 12 ? "AM" : "PM");
  }

  tft.setTextSize(1);
}

// ========== Reset ==========
void resetPongClock() {
  initialized = false;
}

// ========== Tick ==========
void tickPongClock() {
  if (!dpSettings.showClockAfterFinish && !dpSettings.keepDisplayOn) return;
  if (!dispSettings.pongClock) return;

  struct tm now;
  if (!getLocalTime(&now, 0)) return;

  if (!initialized) {
    tft.fillScreen(TFT_BLACK);
    initBricks();
    drawAllBricks();
    spawnBall();
    dispHour = now.tm_hour;
    dispMin = now.tm_min;
    initialized = true;
    lastMinute = now.tm_min;
    for (int i = 0; i < 5; i++) { prevDigitY[i] = 0; digitOffsetY[i] = 0; digitVelocity[i] = 0; }
    for (int i = 0; i < ARK_MAX_FRAGS; i++) frags[i].active = false;
    memset(prevDigits, 0, sizeof(prevDigits));
    prevColon = false;
    prevBallX = -1;
    prevPaddleX = paddleX;
    prevDateStr[0] = '\0';
  }

  if (!timeOverridden) { dispHour = now.tm_hour; dispMin = now.tm_min; }
  if (timeOverridden) {
    bool matches = (now.tm_hour == dispHour && now.tm_min == dispMin);
    bool timeout = (millis() - timeOverrideStart > ARK_TIME_OVERRIDE_MS);
    if (matches || timeout) {
      timeOverridden = false;
      if (timeout && !matches) { dispHour = now.tm_hour; dispMin = now.tm_min; }
    }
  }

  unsigned long ms = millis();
  if (ms - lastUpdateMs < ARK_UPDATE_MS) return;
  lastUpdateMs = ms;

  int sec = now.tm_sec;
  int curMin = now.tm_min;

  if (curMin != lastMinute) { lastMinute = curMin; animTriggered = false; }

  if (sec >= 56 && !animTriggered) {
    animTriggered = true;
    calcTargets(dispHour, dispMin);
    for (int t = 0; t < numTargets; t++) {
      int di = targetDigits[t];
      applyDigitValue(di, targetValues[t]);
      prevDigits[di] = 0;
      triggerBounce(di);
    }
    if (numTargets > 0) {
      timeOverridden = true;
      timeOverrideStart = millis();
    }
  }

  updateBallPhysics();
  updatePaddle();
  updateBounce();

  drawBall();
  drawPaddle();

  // Date
  {
    const char* days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char dateStr[20];
    snprintf(dateStr, sizeof(dateStr), "%s %02d.%02d.%04d",
             days[now.tm_wday], now.tm_mday, now.tm_mon + 1, now.tm_year + 1900);
    if (strcmp(dateStr, prevDateStr) != 0) {
      tft.setFont(FONT_BODY);
      tft.setTextSize(1);
      tft.setTextDatum(top_center);
      tft.setTextColor(TFT_CYAN, TFT_BLACK);
      tft.fillRect(200, ARK_DATE_Y, 400, 30, TFT_BLACK);
      tft.drawString(dateStr, SCREEN_W / 2, ARK_DATE_Y);
      tft.setTextDatum(top_left);
      strlcpy(prevDateStr, dateStr, sizeof(prevDateStr));
    }
  }

  drawTime();
}
