// AI 作業ランプ ファームウェア（CH552T + WS2812C-2020 x19）
//
// PC から USB シリアルで状態を 1 行送ると、その光り方になる（Claude Code のフックから送る。設定は README）
//   work: 青い光が外側をくるくる回る / done: 緑で点きっぱなし / wait: 黄色で点滅 / off: 消す
//   ?   : 今の状態と明るさを返す（例: work 60%）。状態のコマンドには返事をしない（echo で送ったとき返事がループしないように）
// USB を挿すと、緑がふわっと点いて消える（起動したお知らせ）。途中で状態が来たらすぐそっちに変わる
//
// ボタン: 短く押すと消す。長押し（0.6 秒）で明るさを 1 段暗く（100 → 60 → 35 → 20 → 10 → 100%）、フラッシュに保存
// 書き込み: Arduino IDE の「書き込み」でそのまま書ける。書けなくなったら、ボタンを押しながら USB を挿す

#include <string.h>
#include <WS2812.h>

// ---- ここを変えると光り方が変わる ----
// 状態ごとの色（赤, 緑, 青。0〜255）。上から off, work, done, wait
__code uint8_t colors[4][3] = {
  {0, 0, 0},
  {0, 90, 255},
  {0, 255, 40},
  {255, 170, 0},
};
#define SPIN_MS 1200   // 作業中の光が 1 周する時間（ミリ秒）
#define TAIL 4         // 作業中の光の尾の長さ（LED の個数）
#define DIM 40         // 作業中の中心と内側の明るさ（0〜255）
__code uint8_t bootColor[3] = {0, 255, 40};      // 起動したときの色（done と同じ緑）
#define BOOT_IN_MS 800   // 起動したときに明るくなる時間（ミリ秒）
#define BOOT_OUT_MS 1000 // そのあと消えるまでの時間（ミリ秒）
__code uint8_t levels[5] = {255, 153, 89, 51, 26};   // 長押しで切り替える明るさ
__code uint8_t levelPct[5] = {100, 60, 35, 20, 10};
// ------------------------------------

#define PIN_BTN 33     // P3.3
#define PIN_LED 34     // P3.4
#define NUM_LEDS 19    // 0 = 中心、1〜6 = 内側、7〜18 = 外側
#define NUM_BYTES (NUM_LEDS * 3)
#define LONG_PRESS_MS 600
#define EE_MAGIC_ADDR 26   // フラッシュの 26 番地 = 保存済みの印、27 番地 = 明るさ
#define EE_MAGIC 0xE1
#define EE_LEVEL 27

#define OFF 0
#define WORK 1
#define WAIT 3
__code char *names[4] = {"off", "work", "done", "wait"};

// 外側 12 個の LED の番号を、12 時（USB 側）から時計回りに並べたもの
__code uint8_t ring[12] = {9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 7, 8};

__xdata uint8_t ledData[NUM_BYTES];
__xdata uint8_t shownData[NUM_BYTES];   // 最後に LED へ送った中身
__xdata uint8_t state = OFF;
__xdata uint8_t level = 1;              // 60%（机の上で目に入るので少し控えめ）
__xdata uint8_t booting = 1;            // 起動のお知らせ中
__xdata uint8_t btnStable = 1, btnLast = 1, longDone = 0;
__xdata uint32_t btnChangedAt = 0, pressedAt = 0, lastFrame = 0;
__xdata char cmd[16];
__xdata uint8_t cmdLen = 0;

uint8_t scale8(uint8_t v, uint8_t s) {
  return ((uint16_t)v * s) >> 8;
}

// i 番の LED を、今の状態の色で明るさ s（0〜255）にする
void setLed(uint8_t i, uint8_t s) {
  s = scale8(s, levels[level]);
  set_pixel_for_GRB_LED(ledData, i, scale8(colors[state][0], s), scale8(colors[state][1], s), scale8(colors[state][2], s));
}

// 起動のお知らせ: 0 → 明るく → 0。ふわっと見えるように 2 乗のカーブにする
void renderBoot(uint32_t now) {
  uint8_t i;
  uint16_t s;
  if (now < BOOT_IN_MS) s = (uint32_t)now * 255 / BOOT_IN_MS;
  else if (now < BOOT_IN_MS + BOOT_OUT_MS) s = (uint32_t)(BOOT_IN_MS + BOOT_OUT_MS - now) * 255 / BOOT_OUT_MS;
  else {
    booting = 0;
    s = 0;
  }
  s = scale8(scale8(s, s), levels[level]);
  for (i = 0; i < NUM_LEDS; i++)
    set_pixel_for_GRB_LED(ledData, i, scale8(bootColor[0], s), scale8(bootColor[1], s), scale8(bootColor[2], s));
}

void render(uint32_t now) {
  uint8_t i, s = 255;
  uint16_t pos, d;
  memset(ledData, 0, NUM_BYTES);
  if (booting) {
    renderBoot(now);
  } else if (state == WORK) {
    for (i = 0; i < 7; i++) setLed(i, DIM);
    pos = (uint32_t)(now % SPIN_MS) * (12 * 256) / SPIN_MS;   // 先頭の位置（256 = LED 1 個）
    for (i = 0; i < 12; i++) {
      d = (pos + 12 * 256 - i * 256) % (12 * 256);            // 先頭から後ろへの距離
      if (d < TAIL * 256) {
        s = 255 - d / TAIL;
        setLed(ring[i], (uint16_t)s * s / 255);
      } else if (d > 11 * 256) {
        setLed(ring[i], d - 11 * 256);                        // 次に光る LED はじわっと
      }
    }
  } else {
    if (state == WAIT) {   // 1 秒ごとに 0.1 秒で明るく → 0.4 秒点く → 0.1 秒で消える
      d = now % 1000;
      if (d < 100) s = d * 255 / 100;
      else if (d < 500) s = 255;
      else if (d < 600) s = (600 - d) * 255 / 100;
      else s = 0;
    }
    for (i = 0; i < NUM_LEDS; i++) setLed(i, s);
  }
  // LED に送っている間は割り込みが止まるので、変わったときだけ送る
  if (memcmp(ledData, shownData, NUM_BYTES) == 0) return;
  memcpy(shownData, ledData, NUM_BYTES);
  neopixel_show_P3_4(ledData, NUM_BYTES);
}

void pollButton(uint32_t now) {
  uint8_t raw = digitalRead(PIN_BTN);
  if (raw != btnLast) {
    btnLast = raw;
    btnChangedAt = now;
  }
  if (now - btnChangedAt > 20 && raw != btnStable) {   // 20 ms 変わらなければ確定（チャタリング対策）
    btnStable = raw;
    if (raw == 0) {
      pressedAt = now;
      longDone = 0;
    } else if (!longDone) {
      state = OFF;                                       // 短く押したら、離したときに消す
      booting = 0;
    }
  }
  if (btnStable == 0 && !longDone && now - pressedAt >= LONG_PRESS_MS) {
    longDone = 1;                                        // 長押しは押している間に切り替えて、すぐ保存
    level = (level + 1) % 5;
    eeprom_write_byte(EE_LEVEL, level);
    if (eeprom_read_byte(EE_MAGIC_ADDR) != EE_MAGIC) eeprom_write_byte(EE_MAGIC_ADDR, EE_MAGIC);
  }
}

void handleCommand(void) {
  uint8_t i;
  cmd[cmdLen] = 0;
  for (i = 0; i < 4; i++) {
    if (strcmp(cmd, names[i]) == 0) {
      state = i;
      booting = 0;                                       // 起動のお知らせ中でも、すぐ切り替える
    }
  }
  if (cmd[0] == '?') {
    USBSerial_print_s(names[state]);
    USBSerial_print_c(' ');
    USBSerial_print_u(levelPct[level]);
    USBSerial_println_s("%");
    USBSerial_flush();
  }
}

void setup() {
  uint8_t i;
  pinMode(PIN_BTN, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
  delay(30);
  // ボタンを押しながら USB を挿したら、うっすら青く光って書き込みモード（ブートローダ）へ
  if (digitalRead(PIN_BTN) == 0) {
    for (i = 0; i < NUM_LEDS; i++) set_pixel_for_GRB_LED(ledData, i, 0, 0, 20);
    neopixel_show_P3_4(ledData, NUM_BYTES);
    USB_CTRL = 0;
    EA = 0;
    TMOD = 0;
    delayMicroseconds(50000);
    delayMicroseconds(50000);
    __asm__("lcall #0x3800");
    while (1)
      ;
  }
  if (eeprom_read_byte(EE_MAGIC_ADDR) == EE_MAGIC && eeprom_read_byte(EE_LEVEL) < 5) level = eeprom_read_byte(EE_LEVEL);
  shownData[0] = 1;   // 最初の 1 回は必ず送って消す
}

void loop() {
  uint32_t now = millis();
  pollButton(now);
  while (USBSerial_available()) {
    char c = USBSerial_read();
    if (c == '\r' || c == '\n') {
      if (cmdLen) handleCommand();
      cmdLen = 0;
    } else if (cmdLen < sizeof(cmd) - 1) {
      cmd[cmdLen++] = c;
    }
  }
  if (now - lastFrame >= 20) {   // 50 fps
    lastFrame = now;
    render(now);
  }
}
