/*
 * Trough Audio Default code
 * ----------------------------------------------------------------------------
 * modes on a 3.5" 480x320 SPI TFT with resistive touch:
 *   0 - OSCILLOSCOPE
 *   1 - SPECTRUM
 *   2 - STEREO LED
 *
 * The Mode/color/Gain is controlled with 3 buttons
 * ----------------------------------------------------------------------------
 */
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <math.h>

// ============================== USER SETTINGS ===============================
#define PIN_AUDIO_L       34      // ADC1 pin, left  channel (jack tip)
#define PIN_AUDIO_R       35      // ADC1 pin, right channel (jack ring)
#define TFT_ROTATION      1       // 1 = landscape, use 3 if the image is upside down
#define INVERT_COLORS     0       // set to 1 if colors look inverted
#define CALIBRATE_TOUCH   1       // 1 = run touch calibration at boot (first time only)
#define TOUCH_THRESHOLD   350     // lower = more sensitive (good for pencil/stylus)

// Touch calibration data.

static uint16_t calData[5] = { 0, 0, 0, 0, 0 };

// ================================ TUNING ====================================
#define BOOST_SCOPE   1.5f
#define BOOST_BARS    0.8f
#define BOOST_LEDS    0.65f
#define NOISE_GATE    5.0f        // ADC counts ignored by the scope (kills ADC noise)
#define FLOOR_DB      -50.0f      // spectrum: level shown as zero
#define TILT_DB       9.0f        // spectrum: extra dB on the highest band (pink-ish tilt)
#define BAR_FALL      1.4f        // bar fall speed (full scale per second)
#define PEAK_FALL     0.4f        // spectrum peak marker fall speed (per second)
#define LED_FALL      2.5f        // LED column fall speed (full scale per second)
#define PEAK_HOLD_MS  1200        // stereo LED peak hold time
#define PEAK_DROP     12.0f       // stereo LED peak fall speed (segments per second)

static const float GAINS[] = { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f };   // GAIN button steps
#define N_GAINS 5

// ================================ LAYOUT ====================================
#define SCR_W        480
#define SCR_H        320
#define HUD_H        22           // top text strip
#define VIZ_Y        24           // visualizer area: y = 24 .. 267
#define VIZ_H        244
#define BTN_Y        272          // button strip: y = 272 .. 319
#define BTN_H        48
#define BTN_W        150
static const int16_t BTN_X[3] = { 6, 165, 324 };

#define SCOPE_PTS    240          // scope points, 2 px wide each = 480 px
#define SCOPE_PX     2
#define SCOPE_STRIDE 1            // 1 = every sample (~12 ms window at 20 kHz)
#define SCOPE_YC     (VIZ_Y + VIZ_H / 2)
#define SCOPE_HALF   116          // max scope deflection in pixels
#define SCOPE_HALO   2            // soft glow around the trace, in pixels

#define BARS         40           // 40 bars x 12 px = 480 px
#define BAR_PITCH    12
#define BAR_W        10
#define BAR_BASE     (VIZ_Y + VIZ_H - 1)
#define BAR_MAX      236
#define BAR_BANDS    8            // vertical color bands of the bar gradient
#define BAR_BAND_H   30

#define LED_COLS     16           // LED columns per channel
#define LED_ROWS     20
#define LED_CW       12           // cell width
#define LED_PX       14           // column pitch
#define LED_CH       10           // cell height
#define LED_PY       12           // row pitch
#define LED_TOP      (VIZ_Y + 2)
#define LED_X_L      4
#define LED_X_R      252

// ================================== DSP =====================================
#define FFT_N        512          // samples per burst / FFT size
#define HALF_N       (FFT_N / 2)

// ================================ THEMES ====================================
struct Theme { const char* name; bool aggressive; uint32_t a; uint32_t b; };
static const Theme THEMES[] = {
  { "MATRIX",   true,  0x00FF41, 0xD4FFE0 },
  { "BLOOD",    true,  0xFF1F1F, 0xFFB8B0 },
  { "NEON",     true,  0xFF00C8, 0x00E5FF },
  { "CYBER",    true,  0x00F0FF, 0xFF00FF },
  { "FIRE",     true,  0xFF6A00, 0xFFD000 },
  { "AMBER",    false, 0xFFB347, 0xFFF1D6 },
  { "ICE",      false, 0x8FD3FF, 0xEAF8FF },
  { "LAVENDER", false, 0xC7A6FF, 0xF3ECFF },
  { "MINT",     false, 0x9EF5D1, 0xF0FFF9 }
};
#define N_THEMES 9
static const char* MODE_NAMES[3] = { "OSCILLOSCOPE", "SPECTRUM", "STEREO LED" };

// =============================== GLOBALS ====================================
static TFT_eSPI tft = TFT_eSPI();

static uint8_t mode = 0, themeIdx = 0, gIdx = 1;
static uint16_t cA, cB, cDim, cGrid, cUnlit, cGrad[BAR_BANDS];   // colors of the active theme

// --- ADC sampler (core 0) -> double-buffered bursts -------------------------
static int16_t rawL[2][FFT_N], rawR[2][FFT_N];
static volatile uint8_t  pubIdx = 0;       // buffer holding the last complete burst
static volatile uint32_t pubSeq = 0;       // incremented after each complete burst
static volatile float    fsHz   = 20000;   // measured sample rate (per channel)

// --- local copy of the latest burst (DC removed) -----------------------------
static int16_t L[FFT_N], R[FFT_N];

// --- FFT work area + tables ---------------------------------------------------
static float re[FFT_N], im[FFT_N];
static float win[FFT_N];                   // Hann window
static float twC[HALF_N], twS[HALF_N];     // twiddle factors

// --- band edges (FFT bins), rebuilt when the sample rate changes ---------------
static int16_t barEdge[BARS + 1], ledEdge[LED_COLS + 1];
static float   edgeFs = 0;

// --- per-mode state (what is currently drawn on screen) ------------------------
static int16_t sLo[SCOPE_PTS], sHi[SCOPE_PTS];             // scope: drawn span per column
static float   barLvl[BARS], barPk[BARS];                  // spectrum: level / peak
static int16_t barPrevH[BARS], barPrevP[BARS];
static float   ledLvl[2][LED_COLS], ledPk[2][LED_COLS];    // stereo LED: level / peak
static uint32_t ledPkT[2][LED_COLS];
static int8_t  ledPrevLit[2][LED_COLS], ledPrevPk[2][LED_COLS];

// =============================== COLOR HELPERS ===============================
static uint16_t rgb(uint32_t c) {
  return tft.color565((c >> 16) & 255, (c >> 8) & 255, c & 255);
}
// linear mix of two 0xRRGGBB colors, returned as RGB565
static uint16_t mixc(uint32_t c1, uint32_t c2, float t) {
  float r = ((c1 >> 16) & 255) * (1 - t) + ((c2 >> 16) & 255) * t;
  float g = ((c1 >> 8) & 255) * (1 - t) + ((c2 >> 8) & 255) * t;
  float b = (c1 & 255) * (1 - t) + (c2 & 255) * t;
  return tft.color565((uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static void applyTheme() {
  const Theme& t = THEMES[themeIdx];
  cA = rgb(t.a);
  cB = rgb(t.b);
  cDim = mixc(t.a, 0x000000, t.aggressive ? 0.70f : 0.80f);   // trace glow
  for (int i = 0; i < BAR_BANDS; i++) cGrad[i] = mixc(t.a, t.b, i / (float)(BAR_BANDS - 1));
  cUnlit = rgb(0x101010);
  cGrid  = rgb(0x1C1C1C);
}

// ============================== ADC SAMPLER ==================================
// Runs on core 0. Reads FFT_N stereo samples back-to-back (uniform timing is
// what matters for the FFT), publishes the burst, then sleeps 1 tick so the
// idle task / watchdog can run. The real sample rate is measured every burst.
static void samplerTask(void*) {
  uint8_t w = 0;
  for (;;) {
    uint32_t t0 = micros();
    for (int i = 0; i < FFT_N; i++) {
      rawL[w][i] = analogRead(PIN_AUDIO_L);
      rawR[w][i] = analogRead(PIN_AUDIO_R);
    }
    uint32_t dt = micros() - t0;
    if (dt == 0) dt = 1;
    fsHz = 0.7f * fsHz + 0.3f * (FFT_N * 1e6f / (float)dt);
    pubIdx = w;
    pubSeq = pubSeq + 1;
    w ^= 1;
    vTaskDelay(1);
  }
}

// Copies the newest burst into L[] / R[] and removes the DC offset (the ~1.65 V bias).
static bool grabBurst() {
  static uint32_t seen = 0;
  if (pubSeq == seen) return false;
  seen = pubSeq;
  uint8_t b = pubIdx;
  int32_t sl = 0, sr = 0;
  for (int i = 0; i < FFT_N; i++) { sl += rawL[b][i]; sr += rawR[b][i]; }
  int16_t ml = sl / FFT_N, mr = sr / FFT_N;
  for (int i = 0; i < FFT_N; i++) { L[i] = rawL[b][i] - ml; R[i] = rawR[b][i] - mr; }
  return true;
}

// ================================== DSP ======================================
static void initTables() {
  for (int i = 0; i < FFT_N; i++) win[i] = 0.5f * (1.0f - cosf(2.0f * PI * i / (FFT_N - 1)));
  for (int i = 0; i < HALF_N; i++) {
    twC[i] = cosf(2.0f * PI * i / FFT_N);
    twS[i] = sinf(2.0f * PI * i / FFT_N);
  }
}

// In-place radix-2 FFT of re[] / im[] (decimation in time).
static void fft() {
  for (uint16_t i = 1, j = 0; i < FFT_N; i++) {            // bit-reversal permutation
    uint16_t bit = FFT_N >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      float t = re[i]; re[i] = re[j]; re[j] = t;
      t = im[i]; im[i] = im[j]; im[j] = t;
    }
  }
  for (uint16_t len = 2; len <= FFT_N; len <<= 1) {        // butterflies
    uint16_t half = len >> 1, step = FFT_N / len;
    for (uint16_t i = 0; i < FFT_N; i += len) {
      for (uint16_t k = 0; k < half; k++) {
        float wr = twC[k * step], wi = -twS[k * step];
        uint16_t a = i + k, b = a + half;
        float xr = re[b] * wr - im[b] * wi;
        float xi = re[b] * wi + im[b] * wr;
        re[b] = re[a] - xr;  im[b] = im[a] - xi;
        re[a] += xr;         im[a] += xi;
      }
    }
  }
}

// Log-spaced band edges (in FFT bins), 40 Hz .. ~12 kHz, every band >= 1 bin wide.
static void buildEdges(int16_t* edge, int n, float fs) {
  float binHz = fs / FFT_N;
  float f0 = 40.0f, f1 = fminf(fs * 0.5f * 0.92f, 12000.0f);
  edge[0] = max(1, (int)lroundf(f0 / binHz));
  for (int k = 1; k <= n; k++) {
    int e = (int)lroundf(f0 * powf(f1 / f0, (float)k / n) / binHz);
    if (e < edge[k - 1] + 1) e = edge[k - 1] + 1;
    edge[k] = e;
  }
}
static void updateEdges() {
  float fs = fsHz;
  if (edgeFs == 0 || fabsf(fs - edgeFs) > 0.04f * edgeFs) {
    buildEdges(barEdge, BARS, fs);
    buildEdges(ledEdge, LED_COLS, fs);
    edgeFs = fs;
  }
}

// Spectrum of (a) or of the mono mix (a+b)/2 when b != NULL.
// Output: n levels in 0..1 (log/dB scale, so quiet and loud parts both stay visible).
static void bandLevels(const int16_t* a, const int16_t* b, const int16_t* edge, int n,
                       float boost, float* out) {
  for (int i = 0; i < FFT_N; i++) {
    float s = b ? 0.5f * (a[i] + b[i]) : (float)a[i];
    re[i] = s * win[i];
    im[i] = 0;
  }
  fft();
  float scale = 4.0f / (FFT_N * 2048.0f) * boost * GAINS[gIdx];   // 0 dB = full-scale sine
  for (int k = 0; k < n; k++) {
    int lo = edge[k], hi = edge[k + 1];
    float acc = 0;
    for (int j = lo; j < hi; j++) acc += re[j] * re[j] + im[j] * im[j];
    float mag = sqrtf(acc / (hi - lo));
    float db = 20.0f * log10f(mag * scale + 1e-9f) + TILT_DB * k / (float)(n - 1);
    float lv = (db - FLOOR_DB) / (-FLOOR_DB);
    out[k] = lv < 0 ? 0 : (lv > 1 ? 1 : lv);
  }
}

// ============================== UI: HUD + BUTTONS ============================
static void drawHud(bool withFs) {
  char buf[40];
  tft.setTextFont(2);
  tft.setTextColor(cA, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(200);
  snprintf(buf, sizeof(buf), "MODE: %s", MODE_NAMES[mode]);
  tft.drawString(buf, 6, 3, 2);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(200);
  snprintf(buf, sizeof(buf), "%s / %s", THEMES[themeIdx].name,
           THEMES[themeIdx].aggressive ? "AGGRESSIVE" : "SOFT");
  tft.drawString(buf, SCR_W, 3, 2);
  if (withFs) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(cDim, TFT_BLACK);
    tft.setTextPadding(90);
    snprintf(buf, sizeof(buf), "FS %.1fk", fsHz / 1000.0f);
    tft.drawString(buf, SCR_W / 2, 3, 2);
  }
  tft.setTextPadding(0);
}

static void drawButton(int i, bool pressed) {
  char label[24];
  if (i == 0) snprintf(label, sizeof(label), "MODE");
  else if (i == 1) snprintf(label, sizeof(label), "COLOR");
  else snprintf(label, sizeof(label), "GAIN x%g", GAINS[gIdx]);
  uint16_t fill = pressed ? cA : TFT_BLACK;
  uint16_t text = pressed ? TFT_BLACK : cA;
  tft.fillRoundRect(BTN_X[i], BTN_Y, BTN_W, BTN_H, 8, fill);
  tft.drawRoundRect(BTN_X[i], BTN_Y, BTN_W, BTN_H, 8, cA);
  tft.drawRoundRect(BTN_X[i] + 1, BTN_Y + 1, BTN_W - 2, BTN_H - 2, 7, cA);   // 2 px border
  tft.setTextColor(text, fill);
  tft.setTextDatum(MC_DATUM);
  tft.setTextPadding(0);
  tft.drawString(label, BTN_X[i] + BTN_W / 2, BTN_Y + BTN_H / 2, 4);
}
static void drawButtons() { for (int i = 0; i < 3; i++) drawButton(i, false); }

// Clears the visualizer area and resets the "what is drawn" state of every mode.
static void resetViz() {
  tft.fillRect(0, VIZ_Y, SCR_W, VIZ_H, TFT_BLACK);
  for (int i = 0; i < SCOPE_PTS; i++) sLo[i] = -1;
  if (mode == 0) tft.fillRect(0, SCOPE_YC, SCR_W, 1, cGrid);   // scope center line
  for (int i = 0; i < BARS; i++) { barLvl[i] = 0; barPk[i] = 0; barPrevH[i] = 0; barPrevP[i] = -1; }
  for (int c = 0; c < 2; c++)
    for (int i = 0; i < LED_COLS; i++) {
      ledLvl[c][i] = 0; ledPk[c][i] = 0; ledPkT[c][i] = 0;
      ledPrevLit[c][i] = 0; ledPrevPk[c][i] = 0;
    }
  if (mode == 2) {                                                // draw the empty LED matrix once
    for (int c = 0; c < 2; c++)
      for (int i = 0; i < LED_COLS; i++)
        for (int s = 0; s < LED_ROWS; s++)
          tft.fillRect((c ? LED_X_R : LED_X_L) + i * LED_PX,
                       LED_TOP + (LED_ROWS - 1 - s) * LED_PY, LED_CW, LED_CH, cUnlit);
  }
}

// ============================== MODE 0: OSCILLOSCOPE =========================
static void eraseSpan(int x, int a, int b) {
  if (a > b) return;
  tft.fillRect(x, a, SCOPE_PX, b - a + 1, TFT_BLACK);
  if (SCOPE_YC >= a && SCOPE_YC <= b) tft.fillRect(x, SCOPE_YC, SCOPE_PX, 1, cGrid);
}

static void drawScope() {
  static int16_t ys[SCOPE_PTS + 1];

  // rising zero-crossing trigger on the mono mix keeps the wave steady
  int start = 0;
  int searchEnd = FFT_N - SCOPE_PTS * SCOPE_STRIDE - 6;
  for (int i = 2; i < searchEnd; i++) {
    int m0 = (L[i - 1] + R[i - 1]) / 2, m1 = (L[i] + R[i]) / 2;
    int m3 = (L[i + 3] + R[i + 3]) / 2;
    if (m0 < 0 && m1 >= 0 && m3 > m1) { start = i; break; }
  }

  float k = (float)SCOPE_HALF / 2048.0f * BOOST_SCOPE * GAINS[gIdx];
  for (int i = 0; i <= SCOPE_PTS; i++) {
    int idx = start + i * SCOPE_STRIDE;
    float v = 0.5f * (L[idx] + R[idx]);
    if (fabsf(v) < NOISE_GATE) v = 0;
    float d = v * k;
    if (d > SCOPE_HALF) d = SCOPE_HALF;
    if (d < -SCOPE_HALF) d = -SCOPE_HALF;
    ys[i] = SCOPE_YC - (int)d;
  }

  for (int i = 0; i < SCOPE_PTS; i++) {
    int lo = min(ys[i], ys[i + 1]) - 1;          // core: at least 3 px thick
    int hi = max(ys[i], ys[i + 1]) + 1;
    if (lo == sLo[i] && hi == sHi[i]) continue;  // column unchanged
    int x = i * SCOPE_PX;
    int hlo = max(lo - SCOPE_HALO, VIZ_Y), hhi = min(hi + SCOPE_HALO, VIZ_Y + VIZ_H - 1);
    if (sLo[i] >= 0) {                           // erase only what the old glow covered and the new one doesn't
      int ohlo = max(sLo[i] - SCOPE_HALO, VIZ_Y), ohhi = min(sHi[i] + SCOPE_HALO, VIZ_Y + VIZ_H - 1);
      eraseSpan(x, ohlo, min(ohhi, hlo - 1));     // part of the old glow above the new one
      eraseSpan(x, max(ohlo, hhi + 1), ohhi);     // part of the old glow below the new one
    }
    tft.fillRect(x, hlo, SCOPE_PX, hhi - hlo + 1, cDim);      // glow
    tft.fillRect(x, lo, SCOPE_PX, hi - lo + 1, cA);           // bright core
    sLo[i] = lo;
    sHi[i] = hi;
  }
}

// ============================== MODE 1: SPECTRUM =============================
// Draws a distance range [d0, d1) above the bar base using the gradient bands.
static void barFill(int x, int d0, int d1, bool erase) {
  int d = d0;
  while (d < d1) {
    int band = d / BAR_BAND_H;
    if (band >= BAR_BANDS) band = BAR_BANDS - 1;
    int dEnd = min(d1, (band + 1) * BAR_BAND_H);
    tft.fillRect(x, BAR_BASE - dEnd + 1, BAR_W, dEnd - d, erase ? TFT_BLACK : cGrad[band]);
    d = dEnd;
  }
}

static void drawBars(float dt) {
  static float lv[BARS];
  bandLevels(L, R, barEdge, BARS, BOOST_BARS, lv);
  for (int k = 0; k < BARS; k++) {
    float t = lv[k];
    float fall = barLvl[k] - BAR_FALL * dt;
    barLvl[k] = t > fall ? t : fall;                          // smooth discent
    float pf = barPk[k] - PEAK_FALL * dt;
    barPk[k] = barLvl[k] > pf ? barLvl[k] : pf;               //peak marker
    int h = (int)(barLvl[k] * BAR_MAX);
    int p = (int)(barPk[k] * BAR_MAX);
    if (h == barPrevH[k] && p == barPrevP[k]) continue;
    int x = k * BAR_PITCH + 1;
    if (barPrevP[k] >= 0 && p != barPrevP[k])                 // erase the old peak marker
      tft.fillRect(x, BAR_BASE - barPrevP[k] - 3, BAR_W, 3, TFT_BLACK);
    if (h > barPrevH[k]) barFill(x, barPrevH[k], h, false);   // grow
    else if (h < barPrevH[k]) barFill(x, h, barPrevH[k], true); // shrink
    if (p != barPrevP[k]) tft.fillRect(x, BAR_BASE - p - 3, BAR_W, 3, cB);
    barPrevH[k] = h;
    barPrevP[k] = p;
  }
}

// ============================== MODE 2: STEREO LED ===========================
static uint16_t ledColor(int s, int lit, int pk) {
  if (s < lit) return (s >= LED_ROWS * 4 / 5) ? cB : cA;      // top 20 % of the column is brighter
  if (pk > lit && s == pk - 1) return cB;                     // held peak segment
  return cUnlit;
}

static void drawLeds(uint32_t now, float dt) {
  static float lv[LED_COLS];
  for (int c = 0; c < 2; c++) {
    bandLevels(c ? R : L, NULL, ledEdge, LED_COLS, BOOST_LEDS, lv);
    int x0 = c ? LED_X_R : LED_X_L;
    for (int i = 0; i < LED_COLS; i++) {
      float fall = ledLvl[c][i] - LED_FALL * dt;
      ledLvl[c][i] = lv[i] > fall ? lv[i] : fall;
      int lit = (int)(ledLvl[c][i] * LED_ROWS + 0.5f);
      if (lit >= ledPk[c][i]) {                                // new peak: hold it
        ledPk[c][i] = lit;
        ledPkT[c][i] = now;
      } else if (now - ledPkT[c][i] > PEAK_HOLD_MS) {          // after 1200 ms the peak falls
        ledPk[c][i] -= PEAK_DROP * dt;
        if (ledPk[c][i] < lit) ledPk[c][i] = lit;
      }
      int pk = (int)(ledPk[c][i] + 0.5f);
      int pl = ledPrevLit[c][i], pp = ledPrevPk[c][i];
      if (lit == pl && pk == pp) continue;
      int s0 = min(min(pl, lit), min(pp, pk) - 1);
      int s1 = max(max(pl, lit), max(pp, pk)) - 1;
      if (s0 < 0) s0 = 0;
      if (s1 > LED_ROWS - 1) s1 = LED_ROWS - 1;
      for (int s = s0; s <= s1; s++)
        tft.fillRect(x0 + i * LED_PX, LED_TOP + (LED_ROWS - 1 - s) * LED_PY, LED_CW, LED_CH,
                     ledColor(s, lit, pk));
      ledPrevLit[c][i] = lit;
      ledPrevPk[c][i] = pk;
    }
  }
}

// ================================== TOUCH ====================================
//man if this was hard
static void fullRedraw() {
  applyTheme();
  tft.fillScreen(TFT_BLACK);
  resetViz();
  drawHud(true);
  drawButtons();
}

static void handleTap(uint16_t x, uint16_t y) {
  if (y < BTN_Y - 6) return;                                   // taps above the buttons are ignored
  for (int i = 0; i < 3; i++) {
    if (x + 5 >= BTN_X[i] && x <= BTN_X[i] + BTN_W + 5) {      // +-5 px forgiving hit box
      if (i == 0) mode = (mode + 1) % 3;
      else if (i == 1) themeIdx = (themeIdx + 1) % N_THEMES;
      else gIdx = (gIdx + 1) % N_GAINS;
      drawButton(i, true);                                     // quick press feedback
      delay(70);
      if (i == 2) { drawButton(2, false); }
      else { fullRedraw(); }
      return;
    }
  }
}

static void pollTouch() {
  static uint32_t lastPoll = 0, lastTap = 0;
  static bool wasDown = false;
  uint32_t now = millis();
  if (now - lastPoll < 25) return;                             // poll rate at 40 Hz (good enought:))
  lastPoll = now;
  uint16_t tx, ty;
  bool down = tft.getTouch(&tx, &ty, TOUCH_THRESHOLD);
  if (down && !wasDown && now - lastTap > 200) {               // act on the press edge only
    lastTap = now;
    handleTap(tx, ty);
  }
  wasDown = down;
}

// ================================== SETUP ====================================
void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(TFT_ROTATION);
  if (INVERT_COLORS) tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

#if CALIBRATE_TOUCH
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Touch the corner markers", SCR_W / 2, SCR_H / 2, 4);
  tft.calibrateTouch(calData, TFT_MAGENTA, TFT_BLACK, 15);
  Serial.printf("calData = { %u, %u, %u, %u, %u };  <- paste into calData and set CALIBRATE_TOUCH 0\n",
                calData[0], calData[1], calData[2], calData[3], calData[4]);
#endif
  tft.setTouch(calData);

  analogReadResolution(12);                  // 0..4095
  analogSetAttenuation(ADC_11db);            // full 0..3.3 V input range(it has the few components that convert ac to dc)
  initTables();
  updateEdges();

  fullRedraw();
  xTaskCreatePinnedToCore(samplerTask, "adc", 3072, NULL, 1, NULL, 0);

  Serial.printf("Free heap: %u bytes, largest block: %u bytes\n",
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
}

// ================================== LOOP =====================================
void loop() {
  static uint32_t lastFrame = 0, lastHud = 0;
  pollTouch();
  if (!grabBurst()) { delay(1); return; }                      // wait for a fresh audio burst

  uint32_t now = millis();
  float dt = (now - lastFrame) / 1000.0f;
  if (dt > 0.2f) dt = 0.2f;
  lastFrame = now;
  updateEdges();

  if (mode == 0) drawScope();
  else if (mode == 1) drawBars(dt);
  else drawLeds(now, dt);

  if (now - lastHud > 1000) { lastHud = now; drawHud(true); }  // refresh the sample-rate readout
}
//i made this monstrosity in 4 days so i hope that you like it (this time i accually implement full english comments)
