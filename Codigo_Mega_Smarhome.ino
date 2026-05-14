// ============================================================
// SECCION 1: LIBRERIAS
// Importa todas las bibliotecas necesarias para LCD, tactil,
// OLED, RFID, infrarrojos, I2C por software y matematicas.
// ============================================================
#include <TouchScreen.h>
#include <LCDWIKI_GUI.h>
#include <LCDWIKI_KBV.h>
#include <string.h>
#include <U8g2lib.h>
#include <IRremote.h>
#include <SoftwareWire.h>
#include <math.h>
#include <SPI.h>
#include <MFRC522.h>


// ============================================================
// SECCION 2: CONTROL DE FUENTE DE COMANDOS UI
// Controla si los overlays y cambios visuales vienen del
// touch local, de la app (WebSocket) o del modo automatico.
// Evita que comandos remotos sobreescriban la pantalla.
// ============================================================
enum UiCmdSource : uint8_t { UI_SRC_LOCAL = 0, UI_SRC_APP = 1, UI_SRC_AUTO = 2 };
static volatile uint8_t uiCmdSource = UI_SRC_LOCAL;
static inline bool uiAllowOverlays() { return uiCmdSource != UI_SRC_APP; }


// ============================================================
// SECCION 3: DEBOUNCE Y VELOCIDAD DE LA INTERFAZ TACTIL
// Define tiempos minimos entre toques para evitar rebotes
// y bloquea temporalmente el trabajo pesado tras un toque.
// ============================================================
static unsigned long uiLastTouchMs = 0;
static unsigned long uiBlockHeavyUntilMs = 0;
static const uint16_t UI_TOUCH_DEBOUNCE_MS = 35;
static const uint16_t UI_HEAVY_BLOCK_MS    = 350;
static inline bool uiAllowHeavyWorkNow() { return millis() > uiBlockHeavyUntilMs; }


// ============================================================
// SECCION 4: CONFIGURACION RFID (RC522)
// Define los pines, el objeto MFRC522, los UIDs autorizados
// y las variables de estado para la lectura de tarjetas.
// ============================================================
#define RFID_SS_PIN 53
#define RFID_RST_PIN 43
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

const char* UIDS_ACEPTADOS[] = {
  "DA56DF00",
};
const byte NUM_UIDS_OK = sizeof(UIDS_ACEPTADOS) / sizeof(UIDS_ACEPTADOS[0]);

static bool rfidSeen = false;
static bool rfidAccepted = false;
static char rfidUidStr[20] = "";
static bool rfidDrawnOnceInSec = false;
static bool rfidDrawnOnceInRFID = false;

static bool rfidOpenedDoor = false;
static unsigned long rfidDoorCloseAtMs = 0;


// ============================================================
// SECCION 5: PROTOTIPOS DE FUNCIONES
// Declaraciones anticipadas para que el compilador reconozca
// funciones que se definen mas adelante en el archivo.
// ============================================================
void drawTVMenu();
void autoPrintLucesStatus();
void autoPrintFanStatus();
void autoPrintCurtainStatus();
void drawSecurityMenu();


// ============================================================
// SECCION 6: PANTALLA LCD TFT Y PALETA DE COLORES
// Crea el objeto LCD (2.8" TFT 240x320) y define todos los
// colores base y del tema visual de la interfaz.
// ============================================================
LCDWIKI_KBV my_lcd(240, 320, A3, A2, A1, A0, A4);

#define BLACK 0x0000
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define WHITE 0xFFFF
#define GRAY 0x8410
#define ORANGE 0xFD20

#define THEME_BG_TOP 0xFFFF
#define THEME_BG_BOTTOM 0xFFFF
#define THEME_PRIMARY 0x4C9F
#define THEME_PRIMARY_DK 0x245F
#define THEME_CARD 0xE71C
#define THEME_CARD_BDR 0xAD75
#define THEME_TEXT_MAIN WHITE
#define THEME_TEXT_ALT 0x4C9F
#define THEME_TEXT_DARK 0x0000

#define SHADOW 0xC618
#define PANEL THEME_CARD
#define ACCENT THEME_TEXT_ALT


// ============================================================
// SECCION 7: PANTALLA TACTIL (TOUCHSCREEN)
// Define los pines de la pantalla resistiva, crea el objeto
// TouchScreen y establece los valores de calibracion.
// ============================================================
#define YP A3
#define XM A2
#define YM 9
#define XP 8
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

#define TS_LEFT 120
#define TS_RT 900
#define TS_TOP 920
#define TS_BOT 100
#define SWAPXY 1


// ============================================================
// SECCION 8: SISTEMA DE MENUS Y COMUNICACION
// Define el menu activo (currentMenu), el buffer de linea
// para comandos WebSocket y la IP recibida del ESP32.
// ============================================================
int currentMenu = 0;

String _wsLine;
String wsIp = "";


// ============================================================
// SECCION 9: CONTROL PWM - LUCES Y VENTILADOR
// Define pines y funciones para controlar el brillo de las
// luces (LED PWM) y la velocidad/direccion del ventilador
// mediante un puente H (IN1, IN2) con PWM por hardware.
// ============================================================
#define LED_PWM_PIN 45
#define FAN_PWM_PIN 46
#define FAN_IN1_PIN 32
#define FAN_IN2_PIN 33

volatile uint8_t ledDuty = 0;
volatile uint8_t fanDuty = 0;
inline uint8_t pwmToPercent(uint8_t v) {
  return (uint8_t)((v * 100UL + 127) / 255UL);
}
inline void setLedDuty(uint8_t v) {
  ledDuty = v;
  analogWrite(LED_PWM_PIN, v);
}
inline void setFanDuty(uint8_t v) {
  fanDuty = v;
  analogWrite(FAN_PWM_PIN, v);
}
static bool fanForward = true;
inline void fanDirForward() {
  digitalWrite(FAN_IN1_PIN, HIGH);
  digitalWrite(FAN_IN2_PIN, LOW);
  fanForward = true;
}
inline void fanDirReverse() {
  digitalWrite(FAN_IN1_PIN, LOW);
  digitalWrite(FAN_IN2_PIN, HIGH);
  fanForward = false;
}
inline void fanBrake() {
  digitalWrite(FAN_IN1_PIN, HIGH);
  digitalWrite(FAN_IN2_PIN, HIGH);
}
inline void fanCoast() {
  digitalWrite(FAN_IN1_PIN, LOW);
  digitalWrite(FAN_IN2_PIN, LOW);
}


// ============================================================
// SECCION 10: SLIDER VISUAL PARA LUCES
// Constantes de posicion y tamanio del control deslizante
// que aparece en el menu de luces para ajustar el brillo.
// ============================================================
const int SLIDER_X = 30, SLIDER_Y = 100, SLIDER_W = 260, SLIDER_H = 16, KNOB_R = 12;


// ============================================================
// SECCION 11: SENSOR ULTRASONICO (HC-SR04)
// Define pines y la funcion de lectura de distancia en cm
// usando pulsos de 10us y midiendo el tiempo de eco.
// ============================================================
#define TRIG_PIN 23
#define ECHO_PIN 24
long readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long d = pulseIn(ECHO_PIN, HIGH, 30000UL);
  return (long)(d * 0.034f / 2.0f);
}


// ============================================================
// SECCION 12: SERVO PUERTA (Timer1 - Hardware)
// Controla el servo de la puerta principal usando el Timer1
// por interrupcion para generar la senal PWM sin bloquear.
// Permite mover el servo suavemente grado a grado.
// ============================================================
#define SERVO_PIN 49
#define SERVO_TICKS_PER_US (F_CPU / 1000000UL / 8)

const int PUERTA_CERRADA = 180;
const int PUERTA_ABIERTA = 0;

static uint8_t SERVO_OUT_PIN = SERVO_PIN;
static volatile uint16_t servoHighTicks = 3000;
static volatile bool servoPhaseHigh = false;
void servo_begin(uint8_t pin) {
  SERVO_OUT_PIN = pin;
  pinMode(SERVO_OUT_PIN, OUTPUT);
  digitalWrite(SERVO_OUT_PIN, LOW);
  TCCR1A = 0;
  TCCR1B = _BV(WGM12) | _BV(CS11);
  OCR1A = servoHighTicks;
  TIMSK1 = _BV(OCIE1A);
}
void servo_write_us(uint16_t us) {
  if (us < 1000) us = 1000;
  if (us > 2000) us = 2000;
  uint16_t ticks = us * SERVO_TICKS_PER_US;
  uint8_t sreg = SREG;
  noInterrupts();
  servoHighTicks = ticks;
  SREG = sreg;
}
void servo_write_angle(int angle) {
  if (angle < 0) angle = 0;
  if (angle > 180) angle = 180;
  uint16_t us = (uint16_t)map(angle, 0, 180, 1000, 2000);
  servo_write_us(us);
}
int servo_read_angle() {
  uint8_t sreg = SREG;
  noInterrupts();
  uint16_t ticks = servoHighTicks;
  SREG = sreg;
  uint16_t us = ticks / SERVO_TICKS_PER_US;
  return (int)map((long)us, 1000, 2000, 0, 180);
}
ISR(TIMER1_COMPA_vect) {
  if (servoPhaseHigh) {
    digitalWrite(SERVO_OUT_PIN, LOW);
    servoPhaseHigh = false;
    OCR1A = (uint16_t)(40000UL - servoHighTicks);
  } else {
    digitalWrite(SERVO_OUT_PIN, HIGH);
    servoPhaseHigh = true;
    OCR1A = servoHighTicks;
  }
}
static bool puertaAbierta = false;
static bool keypadBloqueado = false;
void moverServoA(int objetivo) {
  int actual = servo_read_angle();
  int paso = (objetivo > actual) ? 1 : -1;
  for (int a = actual; a != objetivo; a += paso) {
    servo_write_angle(a);
    delay(8);
  }
  servo_write_angle(objetivo);
}


// ============================================================
// SECCION 13: SERVO CORTINAS (Timer3 - Servo 360 JOG)
// Controla un servo de rotacion continua para abrir/cerrar
// cortinas usando el Timer3 por interrupcion. Incluye
// funcion de homing al encender y control de jog (pulsos
// cortos de movimiento mientras se mantiene presionado).
// ============================================================
#define CURTAIN_SERVO_PIN 48
#define CURT_MS_PER_REV 1000UL
#define CURTAIN_RUN_MS (2UL * CURT_MS_PER_REV)
#define CURTAIN_HOMING_ON_BOOT 0
#define CURTAIN_HOME_SLOW_US 1425
#define CURTAIN_HOME_MARGIN_MS 300UL
#define CURT_SERVO_TICKS_PER_US (F_CPU / 1000000UL / 8)

static uint8_t CURT_SERVO_OUT_PIN = CURTAIN_SERVO_PIN;
static volatile uint16_t curtServoHighTicks = 3000;
static volatile bool curtServoPhaseHigh = false;
void curt_servo_begin(uint8_t pin) {
  CURT_SERVO_OUT_PIN = pin;
  pinMode(CURT_SERVO_OUT_PIN, OUTPUT);
  digitalWrite(CURT_SERVO_OUT_PIN, LOW);
  TCCR3A = 0;
  TCCR3B = _BV(WGM32) | _BV(CS31);
  OCR3A = curtServoHighTicks;
  TIMSK3 = _BV(OCIE3A);
}
void curt_servo_write_us(uint16_t us) {
  if (us < 1000) us = 1000;
  if (us > 2000) us = 2000;
  uint16_t ticks = us * CURT_SERVO_TICKS_PER_US;
  uint8_t s = SREG;
  noInterrupts();
  curtServoHighTicks = ticks;
  SREG = s;
}
ISR(TIMER3_COMPA_vect) {
  if (curtServoPhaseHigh) {
    digitalWrite(CURT_SERVO_OUT_PIN, LOW);
    curtServoPhaseHigh = false;
    OCR3A = (uint16_t)(40000UL - curtServoHighTicks);
  } else {
    digitalWrite(CURT_SERVO_OUT_PIN, HIGH);
    curtServoPhaseHigh = true;
    OCR3A = curtServoHighTicks;
  }
}
inline void curtainServoStop() { curt_servo_write_us(1500); }
inline void curtainServoFwd() { curt_servo_write_us(1700); }
inline void curtainServoRev() { curt_servo_write_us(1300); }
inline void curtainServoSlowClose() { curt_servo_write_us(CURTAIN_HOME_SLOW_US); }

enum CurtainState { CLOSED, OPEN, MOVING_OPEN, MOVING_CLOSE };
CurtainState curtainState = CLOSED;
unsigned long curtainMotionStart = 0;
enum CurtainJog { JOG_NONE, JOG_OPEN, JOG_CLOSE };
static CurtainJog curtainJog = JOG_NONE;
static unsigned long curtainLastTouchMs = 0;

static inline void autoDrawCurtainStatusBadge(const char* msg) {
  if (currentMenu != 10) return;
  const int x = 10 + 96 + 8 + 96 + 8;
  const int y = 52 + 104;
  const int w = 96;
  const int h = 18;
  my_lcd.Set_Draw_color(THEME_CARD);
  my_lcd.Fill_Rectangle(x + 6, y, x + w - 6, y + h);
  my_lcd.Set_Text_Back_colour(THEME_CARD);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Set_Text_Size(1);
  if (!msg) msg = "--";
  my_lcd.Print_String((char*)msg, x + 10, y + 5);
}

void updateCurtainStatus(const char* forced = nullptr);

void curtainJogStartOpen() {
  if (curtainJog == JOG_OPEN) return;
  curtainServoRev();
  curtainJog = JOG_OPEN;
  curtainMotionStart = millis();

  if (uiAllowOverlays()) {
    if (currentMenu == 10) autoPrintCurtainStatus(); else updateCurtainStatus("Abriendo...");
  }
}
void curtainJogStartClose() {
  if (curtainJog == JOG_CLOSE) return;
  curtainServoFwd();
  curtainJog = JOG_CLOSE;
  curtainMotionStart = millis();

  if (uiAllowOverlays()) {
    if (currentMenu == 10) autoPrintCurtainStatus(); else updateCurtainStatus("Cerrando...");
  }
}
void curtainJogStop() {
  if (curtainJog == JOG_NONE) return;
  curtainServoStop();
  if (curtainJog == JOG_OPEN) curtainState = OPEN;
  if (curtainJog == JOG_CLOSE) curtainState = CLOSED;
  curtainJog = JOG_NONE;

  if (uiAllowOverlays()) {
    if (currentMenu == 10) { autoPrintCurtainStatus(); } else { updateCurtainStatus(nullptr); }
  }
}
void curtainService() {
  if (curtainJog != JOG_NONE) {
    if (millis() - curtainMotionStart >= CURTAIN_RUN_MS) {
      curtainJogStop();
    }
  }
}
void curtainHomeOnBoot() {
#if CURTAIN_HOMING_ON_BOOT
  curtainServoSlowClose();
  delay(CURTAIN_RUN_MS + CURTAIN_HOME_MARGIN_MS);
  curtainServoStop();
  curtainState = CLOSED;
#endif
}


// ============================================================
// SECCION 14: MICROFONO (DETECCION DE APLAUSOS)
// Detecta senales digitales del microfono para alternar
// el estado de las luces al detectar un aplauso.
// ============================================================
#define MIC_PIN 40
unsigned long lastClapMs = 0;
const unsigned long CLAP_DEBOUNCE_MS = 400;
int micPrev = LOW;
void onLedDutyChangedUI();

void handleClapToggle() {
  int micNow = digitalRead(MIC_PIN);
  if (micPrev == LOW && micNow == HIGH && (millis() - lastClapMs) > CLAP_DEBOUNCE_MS) {
    lastClapMs = millis();
    setLedDuty((ledDuty < 128) ? 255 : 0);
    onLedDutyChangedUI();
    if (currentMenu == 3) { /* UI refresco */ }
  }
  micPrev = micNow;
}


// ============================================================
// SECCION 15: HELPERS DE TEXTO Y GRAFICOS UI
// Funciones utilitarias para: convertir texto a mayusculas,
// comparar cadenas sin importar mayusculas/minusculas,
// dibujar degradados verticales, botones redondeados
// y tarjetas (cards) en la pantalla LCD.
// ============================================================
void toUpperInPlace(char* s) {
  for (char* p = s; *p; ++p)
    if (*p >= 'a' && *p <= 'z') *p = *p - 'a' + 'A';
}
bool equalsIgnoreCase(const char* a, const char* b) {
  while (*a && *b) {
    char ca = (*a >= 'a' && *a <= 'z') ? *a - 32 : *a, cb = (*b >= 'a' && *b <= 'z') ? *b - 32 : *b;
    if (ca != cb) return false;
    ++a;
    ++b;
  }
  return *a == *b;
}

uint16_t lerp565(uint16_t c1, uint16_t c2, uint16_t i, uint16_t steps) {
  uint8_t r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  uint8_t r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
  uint8_t r = ((uint32_t)r1 * (steps - 1 - i) + (uint32_t)r2 * i) / (steps - 1);
  uint8_t g = ((uint32_t)g1 * (steps - 1 - i) + (uint32_t)g2 * i) / (steps - 1);
  uint8_t b = ((uint32_t)b1 * (steps - 1 - i) + (uint32_t)b2 * i) / (steps - 1);
  return ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
}
void drawGradientV(uint16_t topColor, uint16_t bottomColor) {
  int w = my_lcd.Get_Display_Width();
  int h = my_lcd.Get_Display_Height();
  for (int y = 0; y < h; y++) {
    uint16_t c = lerp565(topColor, bottomColor, y, h);
    my_lcd.Set_Draw_color(c);
    my_lcd.Fill_Rectangle(0, y, w - 1, y);
  }
}

void drawButton(int x, int y, int w, int h, const char* label, uint16_t color) {
  int r = h / 2, cxL = x + r, cxR = x + w - r, cy = y + h / 2;
  my_lcd.Set_Draw_color(color);
  my_lcd.Set_Text_Back_colour(color);
  my_lcd.Fill_Rectangle(x + r, y, x + w - r, y + h);
  my_lcd.Fill_Circle(cxL, cy, r);
  my_lcd.Fill_Circle(cxR, cy, r);
  my_lcd.Set_Text_Size(1);
  my_lcd.Set_Text_colour(THEME_TEXT_MAIN);
  char buf[40];
  strncpy(buf, label, sizeof(buf));
  buf[sizeof(buf) - 1] = '\0';
  toUpperInPlace(buf);
  int charW = 6, charH = 8, textW = strlen(buf) * 6;
  int tx = x + (w - textW) / 2, ty = y + (h - charH) / 2 + 2;
  my_lcd.Print_String(buf, tx, ty);
}
void drawButtonColored(int x, int y, int w, int h, const char* label, uint16_t color) { drawButton(x, y, w, h, label, color); }

static inline void fillRoundRect(int x, int y, int w, int h, int r, uint16_t col) {
  my_lcd.Set_Draw_color(col);
  my_lcd.Fill_Rectangle(x + r, y, x + w - r, y + h);
  my_lcd.Fill_Rectangle(x, y + r, x + w, y + h - r);
  my_lcd.Fill_Circle(x + r, y + r, r);
  my_lcd.Fill_Circle(x + w - r, y + r, r);
  my_lcd.Fill_Circle(x + r, y + h - r, r);
  my_lcd.Fill_Circle(x + w - r, y + h - r, r);
}
static inline void drawRoundRect(int x, int y, int w, int h, int r, uint16_t col) {
  my_lcd.Set_Draw_color(col);
  my_lcd.Draw_Rectangle(x + r, y, x + w - r, y);
  my_lcd.Draw_Rectangle(x + r, y + h, x + w - r, y + h);
  my_lcd.Draw_Rectangle(x, y + r, x, y + h - r);
  my_lcd.Draw_Rectangle(x + w, y + r, x + w, y + h - r);
  my_lcd.Draw_Circle(x + r, y + r, r);
  my_lcd.Draw_Circle(x + w - r, y + r, r);
  my_lcd.Draw_Circle(x + r, y + h - r, r);
  my_lcd.Draw_Circle(x + w - r, y + h - r, r);
}
static inline void drawCard(int x, int y, int w, int h, const char* title) {
  const int r = 10;
  fillRoundRect(x + 3, y + 3, w, h, r, SHADOW);
  fillRoundRect(x, y, w, h, r, THEME_CARD);
  drawRoundRect(x, y, w, h, r, THEME_CARD_BDR);
  my_lcd.Set_Text_Back_colour(THEME_CARD);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Set_Text_Size(1);
  int tw = strlen(title) * 6;
  my_lcd.Print_String((char*)title, x + (w - tw) / 2, y + 6);
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Draw_Rectangle(x + 12, y + 18, x + w - 12, y + 18);
}


// ============================================================
// SECCION 16: ICONOS COMPACTOS PARA PANTALLA AUTOMATICA
// Dibuja iconos pequenos de bombilla, ventilador y cortina
// dentro de las tarjetas del modo automatico en el LCD.
// ============================================================
static inline void iconBulbSmall(int cx, int cy) {
  my_lcd.Set_Draw_color(THEME_TEXT_ALT);
  my_lcd.Draw_Circle(cx, cy - 5, 8);
  my_lcd.Draw_Rectangle(cx - 5, cy + 3, cx + 5, cy + 8);
  my_lcd.Draw_Rectangle(cx - 3, cy + 8, cx + 3, cy + 11);
  my_lcd.Draw_Rectangle(cx - 2, cy + 11, cx + 2, cy + 13);
}
static inline void iconFanSmall(int cx, int cy) {
  my_lcd.Set_Draw_color(THEME_TEXT_ALT);
  my_lcd.Draw_Circle(cx, cy, 8);
  my_lcd.Draw_Line(cx, cy, cx + 8, cy);
  my_lcd.Draw_Line(cx, cy, cx - 4, cy + 7);
  my_lcd.Draw_Line(cx, cy, cx - 4, cy - 7);
}
static inline void iconCurtainCompact(int x1, int y1, int w) {
  const int h = 36;
  my_lcd.Set_Draw_color(THEME_TEXT_ALT);
  my_lcd.Draw_Rectangle(x1 + 6, y1 + 20, x1 + w - 6, y1 + 20);
  for (int i = 0; i < 5; i++) {
    int lx = x1 + 10 + i * ((w - 20) / 4);
    my_lcd.Draw_Rectangle(lx, y1 + 22, lx, y1 + 20 + h - 8);
  }
  my_lcd.Draw_Rectangle(x1 + 8, y1 + 20 + h - 8, x1 + w - 8, y1 + 20 + h - 8);
}


// ============================================================
// SECCION 17: ETIQUETA DE IP EN PANTALLA
// Muestra la IP del ESP32 (recibida por Serial1) en la
// esquina superior izquierda del menu principal.
// ============================================================
void drawIpLabel() {
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(0, 0, 160, 20);

  my_lcd.Set_Text_Size(1);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);

  String ipLine = "IP: ";
  ipLine += (wsIp.length() ? wsIp : "--");
  my_lcd.Print_String(ipLine.c_str(), 5, 5);
}


// ============================================================
// SECCION 18: MENUS PRINCIPALES Y SUBMENUS
// Dibuja el menu principal ("Manual" / "Automatico") y el
// submenu con accesos a Garaje, Luces, Ventilador, Puerta,
// Cortinas y TV.
// ============================================================
void drawMainMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  drawIpLabel();

  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("Smart Home", 100, 20);

  drawButton(110, 80, 100, 50, "Manual", THEME_PRIMARY);
  drawButton(110, 140, 100, 50, "Automatico", THEME_PRIMARY);
}

void drawSubMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("Menu Secundario", 76, 20);
  drawButton(20, 60, 80, 50, "Garaje", THEME_PRIMARY);
  drawButton(120, 60, 80, 50, "Luces", THEME_PRIMARY);
  drawButton(220, 60, 80, 50, "Ventilador", THEME_PRIMARY);
  drawButton(20, 130, 80, 50, "Puerta", THEME_PRIMARY);
  drawButton(120, 130, 80, 50, "Cortinas", THEME_PRIMARY);
  drawButton(220, 130, 80, 50, "TV", THEME_PRIMARY);
  drawButton(110, 190, 100, 40, "VOLVER", THEME_PRIMARY);
}


// ============================================================
// SECCION 19: MENU DE LUCES (SLIDER PWM)
// Dibuja y gestiona el slider tactil para controlar el brillo
// de las luces. Tambien muestra el valor de lux del sensor
// BH1750 en tiempo real dentro del mismo menu.
// ============================================================
void drawLedSliderTrack() {
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Fill_Rectangle(SLIDER_X, SLIDER_Y, SLIDER_X + SLIDER_W, SLIDER_Y + SLIDER_H);
  my_lcd.Set_Draw_color(THEME_TEXT_DARK);
  my_lcd.Fill_Rectangle(SLIDER_X + 1, SLIDER_Y + SLIDER_H / 2 - 2, SLIDER_X + SLIDER_W - 1, SLIDER_Y + SLIDER_H / 2 + 2);
}
void drawLedSliderKnob(uint8_t value) {
  int xMin = SLIDER_X, xMax = SLIDER_X + SLIDER_W;
  int knobX = map(value, 0, 255, xMin, xMax), cy = SLIDER_Y + SLIDER_H / 2;
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(SLIDER_X - (KNOB_R + 2), SLIDER_Y - (KNOB_R + 2), SLIDER_X + SLIDER_W + (KNOB_R + 2), SLIDER_Y + SLIDER_H + (KNOB_R + 2));
  drawLedSliderTrack();
  my_lcd.Set_Draw_color(THEME_TEXT_ALT);
  my_lcd.Fill_Circle(knobX, cy, KNOB_R);
}
void drawLedPercent(uint8_t value) {
  uint8_t pct = pwmToPercent(value);
  char buf[16];
  snprintf(buf, sizeof(buf), "%u%%", pct);
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(120, 150, 220, 170);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Set_Text_Size(2);
  my_lcd.Print_String("Brillo:", 40, 150);
  my_lcd.Print_String(buf, 140, 150);
}
static const int LUX_LABEL_Y = 175;
void drawLuxInitInLedMenu() {
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(40, LUX_LABEL_Y, 280, LUX_LABEL_Y + 16);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Set_Text_Size(1);
  my_lcd.Print_String("Lux: ----", 40, LUX_LABEL_Y);
}
void updateLuxInLedMenu(float lux, bool ok) {
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(90, LUX_LABEL_Y, 240, LUX_LABEL_Y + 16);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Set_Text_Size(1);
  if (ok) {
    char b[20];
    dtostrf(lux, 5, 1, b);
    my_lcd.Print_String(b, 90, LUX_LABEL_Y);
  } else {
    my_lcd.Print_String("--.-", 90, LUX_LABEL_Y);
  }
}

void onLedDutyChangedUI() {
  if (!uiAllowOverlays()) return;
  if (currentMenu == 3) {
    drawLedSliderKnob(ledDuty);
    drawLedPercent(ledDuty);
  }
}

void drawLedMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Print_String("LUCES", 120, 20);
  drawLedSliderTrack();
  drawLedSliderKnob(ledDuty);
  drawLedPercent(ledDuty);
  drawLuxInitInLedMenu();
  drawButton(20, 200, 80, 40, "APAGAR", THEME_PRIMARY);
  drawButton(110, 200, 100, 40, "VOLVER", THEME_PRIMARY);
}
void handleLedTouch(int tx, int ty) {
  if (ty >= SLIDER_Y - (KNOB_R + 4) && ty <= SLIDER_Y + SLIDER_H + (KNOB_R + 4) && tx >= SLIDER_X - (KNOB_R + 4) && tx <= SLIDER_X + SLIDER_W + (KNOB_R + 4)) {
    int cx = constrain(tx, SLIDER_X, SLIDER_X + SLIDER_W);
    uint8_t v = (uint8_t)map(cx, SLIDER_X, SLIDER_X + SLIDER_W, 0, 255);
    setLedDuty((uint8_t)v);
    drawLedSliderKnob(ledDuty);
    drawLedPercent(ledDuty);
  }
}


// ============================================================
// SECCION 20: MENU DE VENTILADOR
// Dibuja el menu con botones de velocidad (BAJO/MEDIO/ALTO)
// y APAGAR. Muestra el estado actual en una barra de texto.
// ============================================================
int fanLevel = 0;
void drawFanHeader() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("VENTILADOR", 100, 20);
  drawButton(20, 90, 80, 50, "BAJO", THEME_PRIMARY);
  drawButton(120, 90, 80, 50, "MEDIO", THEME_PRIMARY);
  drawButton(220, 90, 80, 50, "ALTO", THEME_PRIMARY);
  drawButton(50, 160, 80, 50, "APAGAR", THEME_PRIMARY);
  drawButton(180, 160, 80, 50, "VOLVER", THEME_PRIMARY);
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Draw_Rectangle(20, 50, 300, 70);
}
void updateFanStateLabel() {
  if (!uiAllowOverlays()) return;
  if (currentMenu != 5) return;
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Fill_Rectangle(90, 51, 295, 69);
  const char* estado = (fanLevel == 0) ? "APAGADO" : (fanLevel == 1) ? "BAJO"
                                               : (fanLevel == 2) ? "MEDIO"
                                                                 : "ALTO";
  my_lcd.Print_String("Estado:", 30, 55);
  my_lcd.Print_String((char*)estado, 120, 55);
}
void drawFanMenu() { drawFanHeader(); updateFanStateLabel(); }
void setFanSpeed(int level) {
  fanLevel = constrain(level, 0, 3);
  setFanDuty((level == 1) ? 110 : (level == 2) ? 170 : (level == 3) ? 255 : 0);
  updateFanStateLabel();
}


// ============================================================
// SECCION 21: MENU Y CONTROL DE GARAJE (Timer4 - Servo 360)
// Servo de rotacion continua para el garaje usando Timer4.
// Incluye deteccion de obstaculos con ultrasonico: si detecta
// algo al cerrar, pausa y reanuda cuando el camino este libre.
// ============================================================
#define GARAGE_SERVO_PIN 42
#define GARAGE_MS_PER_REV 600UL
#define GARAGE_RUN_MS (2UL * GARAGE_MS_PER_REV)
#define GARAGE_US_FWD 1300
#define GARAGE_US_REV 1700
#define GARAGE_US_STOP 1500
#define GARAGE_OBS_CM 10
#define GAR_SERVO_TICKS_PER_US (F_CPU / 1000000UL / 8)
static uint8_t GAR_SERVO_OUT_PIN = GARAGE_SERVO_PIN;
static volatile uint16_t garServoHighTicks = 3000;
static volatile bool garServoPhaseHigh = false;
void gar_servo_begin(uint8_t pin) {
  GAR_SERVO_OUT_PIN = pin;
  pinMode(GAR_SERVO_OUT_PIN, OUTPUT);
  digitalWrite(GAR_SERVO_OUT_PIN, LOW);
  TCCR4A = 0;
  TCCR4B = _BV(WGM42) | _BV(CS41);
  OCR4A = garServoHighTicks;
  TIMSK4 = _BV(OCIE4A);
}
void gar_servo_write_us(uint16_t us) {
  if (us < 1000) us = 1000;
  if (us > 2000) us = 2000;
  uint16_t ticks = us * GAR_SERVO_TICKS_PER_US;
  uint8_t s = SREG;
  noInterrupts();
  garServoHighTicks = ticks;
  SREG = s;
}
ISR(TIMER4_COMPA_vect) {
  if (garServoPhaseHigh) {
    digitalWrite(GAR_SERVO_OUT_PIN, LOW);
    garServoPhaseHigh = false;
    OCR4A = (uint16_t)(40000UL - garServoHighTicks);
  } else {
    digitalWrite(GAR_SERVO_OUT_PIN, HIGH);
    garServoPhaseHigh = true;
    OCR4A = garServoHighTicks;
  }
}
inline void garageServoStop() { gar_servo_write_us(GARAGE_US_STOP); }
inline void garageServoOpen() { gar_servo_write_us(GARAGE_US_FWD); }
inline void garageServoClose() { gar_servo_write_us(GARAGE_US_REV); }

enum GarageState { GARAGE_IDLE, GARAGE_OPENING, GARAGE_CLOSING, GARAGE_PAUSED_OBS, GARAGE_OPENED, GARAGE_CLOSED };
GarageState garageState = GARAGE_CLOSED;
static bool garageCycleActive = false;
static unsigned long garageMotionStartMs = 0;
static unsigned long garageRemainingMs = 0;

static const int GAR_STATUS_X = 204;
static const int GAR_STATUS_Y = 24;
static const int GAR_STATUS_W = 112;
static const int GAR_STATUS_H = 12;
static inline void updateGarageStatus(const char* txt) {
  if (!uiAllowOverlays()) return;
  if (currentMenu != 4) return;
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(GAR_STATUS_X, GAR_STATUS_Y - 8, GAR_STATUS_X + GAR_STATUS_W, GAR_STATUS_Y + 6);
  my_lcd.Set_Text_Size(1);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  char buf[40];
  snprintf(buf, sizeof(buf), "Estado: %s", txt);
  my_lcd.Print_String(buf, GAR_STATUS_X, GAR_STATUS_Y - 4);
}

void updateGarageDistance(long d) {
  if (currentMenu != 4) return;
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(160, 56, 300, 76);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  char buf[20];
  if (d == 0) sprintf(buf, "-- cm");
  else sprintf(buf, "%ld cm", d);
  my_lcd.Print_String(buf, 160, 56);
}

void garageStartOpen() {
  if (garageCycleActive || garageState == GARAGE_OPENING || garageState == GARAGE_OPENED) {
    updateGarageStatus(garageState == GARAGE_OPENED ? "Abierto" : "Abriendo...");
    return;
  }
  garageCycleActive = true;
  garageServoOpen();
  garageState = GARAGE_OPENING;
  garageMotionStartMs = millis();
  garageRemainingMs = GARAGE_RUN_MS;
  updateGarageStatus("Abriendo...");
}
void garageStartClose() {
  if (garageCycleActive || garageState == GARAGE_CLOSING || garageState == GARAGE_CLOSED) {
    updateGarageStatus(garageState == GARAGE_CLOSED ? "Cerrado" : "Cerrando...");
    return;
  }
  long d = readUltrasonic();
  if (d > 0 && d < GARAGE_OBS_CM) {
    garageServoStop();
    garageState = GARAGE_PAUSED_OBS;
    garageRemainingMs = GARAGE_RUN_MS;
    updateGarageStatus("Obstaculo");
    return;
  }
  garageCycleActive = true;
  garageServoClose();
  garageState = GARAGE_CLOSING;
  garageMotionStartMs = millis();
  garageRemainingMs = GARAGE_RUN_MS;
  updateGarageStatus("Cerrando...");
}
void garageStopAll() {
  garageServoStop();
  if (garageState == GARAGE_OPENING) garageState = GARAGE_OPENED;
  else if (garageState == GARAGE_CLOSING) garageState = GARAGE_CLOSED;
  else if (garageState == GARAGE_PAUSED_OBS) garageState = GARAGE_CLOSED;
  else garageState = GARAGE_IDLE;
}
void garageService() {
  if (currentMenu == 4) {
    static unsigned long last = 0;
    if (millis() - last >= 400) {
      last = millis();
      updateGarageDistance(readUltrasonic());
    }
  }
  unsigned long now = millis();
  if (garageState == GARAGE_OPENING) {
    unsigned long elapsed = now - garageMotionStartMs;
    if (elapsed >= garageRemainingMs) {
      garageServoStop();
      garageState = GARAGE_OPENED;
      garageCycleActive = false;
      updateGarageStatus("Abierto");
    }
  } else if (garageState == GARAGE_CLOSING) {
    long d = readUltrasonic();
    if (d > 0 && d < GARAGE_OBS_CM) {
      unsigned long elapsed = now - garageMotionStartMs;
      if (elapsed >= garageRemainingMs) elapsed = garageRemainingMs;
      garageRemainingMs -= elapsed;
      garageServoStop();
      garageState = GARAGE_PAUSED_OBS;
      updateGarageStatus("Obstaculo");
      return;
    }
    unsigned long elapsed = now - garageMotionStartMs;
    if (elapsed >= garageRemainingMs) {
      garageServoStop();
      garageState = GARAGE_CLOSED;
      garageCycleActive = false;
      updateGarageStatus("Cerrado");
    }
  } else if (garageState == GARAGE_PAUSED_OBS) {
    long d = readUltrasonic();
    if (d >= GARAGE_OBS_CM || d == 0) {
      garageServoClose();
      garageState = GARAGE_CLOSING;
      garageMotionStartMs = now;
      updateGarageStatus("Cerrando...");
    }
  }
}

void drawGarageMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Print_String("GARAJE", 120, 20);
  my_lcd.Print_String("Distancia:", 20, 56);
  my_lcd.Print_String("-- cm", 160, 56);
  drawButton(40, 90, 100, 60, "ABRIR", THEME_PRIMARY);
  drawButton(180, 90, 100, 60, "CERRAR", THEME_PRIMARY);
  drawButton(110, 170, 100, 40, "VOLVER", THEME_PRIMARY);
  if (garageState == GARAGE_CLOSED) updateGarageStatus("Cerrado");
  else if (garageState == GARAGE_OPENED) updateGarageStatus("Abierto");
  else updateGarageStatus("--");
}


// ============================================================
// SECCION 22: TV - OLED, ANIMACION POR CANALES E INFRARROJOS
// Controla la pantalla OLED (SSD1306 128x64) con animaciones
// de canales (casa, arbol, auto, estrella) usando millis()
// sin bloquear. El control remoto IR maneja encendido,
// canales y volumen. El menu TFT replica los mismos controles.
// ============================================================
bool tvPower = false;
int tvChannel = 1;
const int TV_CHANNELS = 4;
int tvVolLevel = 25;
const int TV_VOL_MIN = 0, TV_VOL_MAX = 50;

#define OLED_SCL 26
#define OLED_SDA 27
#define OLED_RST U8X8_PIN_NONE
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, OLED_RST);

static uint8_t  tvFrame = 0;
static unsigned long tvAnimLastMs = 0;
static const uint16_t TV_ANIM_DT_MS = 120;

static inline void drawChannelHouseFrame(uint8_t f) {
  u8g2.drawFrame(44, 28, 40, 22);
  u8g2.drawTriangle(44, 28, 84, 28, 64, 14);
  u8g2.drawFrame(60, 34, 8, 16);

  if (f & 1) {
    u8g2.drawBox(50, 34, 6, 6);
    u8g2.drawBox(72, 34, 6, 6);
  } else {
    u8g2.drawFrame(50, 34, 6, 6);
    u8g2.drawFrame(72, 34, 6, 6);
  }

  uint8_t y = 18 + (f % 3);
  u8g2.drawCircle(78, y, 2);
  u8g2.drawCircle(82, y - 3, 1);
}

static inline void drawChannelTreeFrame(uint8_t f) {
  u8g2.drawBox(62, 36, 4, 14);

  int dx = (f & 1) ? 1 : -1;
  u8g2.drawCircle(64 + dx, 30, 10);
  u8g2.drawCircle(56 + dx, 28, 7);
  u8g2.drawCircle(72 + dx, 28, 7);

  if (f & 2) {
    u8g2.drawLine(30, 40, 40, 40);
    u8g2.drawLine(32, 44, 42, 44);
  }
}

static inline void drawChannelCarFrame(uint8_t f) {
  u8g2.drawFrame(48, 38, 32, 10);
  u8g2.drawFrame(54, 32, 20, 8);

  u8g2.drawCircle(54, 50, 3);
  u8g2.drawCircle(74, 50, 3);
  if (f & 1) {
    u8g2.drawLine(54, 50, 57, 50);
    u8g2.drawLine(74, 50, 77, 50);
  } else {
    u8g2.drawLine(54, 50, 54, 47);
    u8g2.drawLine(74, 50, 74, 47);
  }

  if (f & 2) {
    u8g2.drawLine(40, 42, 46, 42);
    u8g2.drawLine(38, 46, 46, 46);
  }
}

static inline void drawChannelStarFrame(uint8_t f) {
  const uint8_t cx = 64, cy = 38, r = 12;

  u8g2.drawLine(cx - r, cy, cx + r, cy);
  u8g2.drawLine(cx, cy - r, cx, cy + r);
  u8g2.drawLine(cx - r + 2, cy - r + 2, cx + r - 2, cy + r - 2);
  u8g2.drawLine(cx - r + 2, cy + r - 2, cx + r - 2, cy - r + 2);

  if (f & 1) {
    u8g2.drawCircle(cx, cy, 2);
    u8g2.drawPixel(cx - 6, cy - 6);
    u8g2.drawPixel(cx + 6, cy - 4);
    u8g2.drawPixel(cx - 4, cy + 6);
  } else {
    u8g2.drawPixel(cx, cy);
  }
}

static inline void tvRenderOLEDFrame(uint8_t frameIdx) {
  u8g2.clearBuffer();

  if (!tvPower) {
    u8g2.drawPixel(127, 63);
    u8g2.sendBuffer();
    return;
  }

  const char* chName = (tvChannel == 1) ? "CASA" : (tvChannel == 2) ? "ARBOL" : (tvChannel == 3) ? "AUTO" : "ESTRELLA";

  char leftBuf[22];
  snprintf(leftBuf, sizeof(leftBuf), "CAN %d %s", tvChannel, chName);

  char rightBuf[18];
  snprintf(rightBuf, sizeof(rightBuf), "VOL %d/50", tvVolLevel);

  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, leftBuf);
  uint8_t w = u8g2.getStrWidth(rightBuf);
  u8g2.drawStr(128 - w, 10, rightBuf);

  switch (tvChannel) {
    case 1: drawChannelHouseFrame(frameIdx); break;
    case 2: drawChannelTreeFrame(frameIdx); break;
    case 3: drawChannelCarFrame(frameIdx); break;
    case 4: drawChannelStarFrame(frameIdx); break;
  }

  u8g2.sendBuffer();
}

void tvRenderOLED() { tvRenderOLEDFrame(tvFrame); }

void tvServiceOLED() {
  if (!tvPower) return;
  unsigned long now = millis();
  if (now - tvAnimLastMs >= TV_ANIM_DT_MS) {
    tvAnimLastMs = now;
    tvFrame++;
    tvRenderOLEDFrame(tvFrame);
  }
}

void drawTVMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("TV", 145, 20);
  drawButtonColored(20, 60, 100, 50, "ENC/APAG", THEME_PRIMARY);
  drawButton(130, 60, 80, 40, "CAN-", THEME_PRIMARY);
  drawButton(220, 60, 80, 40, "CAN+", THEME_PRIMARY);
  drawButton(130, 110, 80, 40, "VOL-", THEME_PRIMARY);
  drawButton(220, 110, 80, 40, "VOL+", THEME_PRIMARY);
  drawButton(20, 190, 100, 40, "VOLVER", THEME_PRIMARY);
  tvRenderOLED();
}


// ============================================================
// SECCION 23: SENSOR DE TEMPERATURA MLX90614 (I2C SOFTWARE)
// Lee la temperatura del objeto en grados Celsius desde el
// sensor infrarrojo MLX90614 usando I2C por software.
// ============================================================
#define MLX90614_ADDR 0x5A
#define MLX90614_OBJ1 0x07
#define MLX_SDA_PIN 28
#define MLX_SCL_PIN 29
SoftwareWire mlxWire(MLX_SDA_PIN, MLX_SCL_PIN);
bool mlxReady = false;
bool mlxRead16(uint8_t reg, uint16_t& out) {
  mlxWire.beginTransmission(MLX90614_ADDR);
  mlxWire.write(reg);
  if (mlxWire.endTransmission(false) != 0) return false;
  if (mlxWire.requestFrom((uint8_t)MLX90614_ADDR, (uint8_t)3) != 3) return false;
  uint8_t l = mlxWire.read(), h = mlxWire.read();
  (void)mlxWire.read();
  out = ((uint16_t)h << 8) | l;
  return true;
}
bool mlxReadObjectC(float& tC) {
  uint16_t raw;
  if (!mlxRead16(MLX90614_OBJ1, raw)) return false;
  float tK = (float)raw * 0.02f;
  tC = tK - 273.15f;
  return true;
}


// ============================================================
// SECCION 24: SENSOR DE LUZ BH1750 (I2C SOFTWARE)
// Lee la iluminancia en lux desde el sensor BH1750 usando
// I2C por software. Usado tanto en el menu de luces como
// en el modo automatico para control por umbral de luz.
// ============================================================
#define BH1750_ADDR 0x23
#define BH1750_PWR_ON 0x01
#define BH1750_RESET 0x07
#define BH1750_CONT_HR 0x10
#define BH_SDA_PIN 34
#define BH_SCL_PIN 35
SoftwareWire bhWire(BH_SDA_PIN, BH_SCL_PIN);
bool bhReady = false;
bool bh1750WriteCmd(uint8_t cmd) {
  bhWire.beginTransmission(BH1750_ADDR);
  bhWire.write(cmd);
  return (bhWire.endTransmission() == 0);
}
bool bh1750Begin() {
  bool ok1 = bh1750WriteCmd(BH1750_PWR_ON);
  bool ok2 = bh1750WriteCmd(BH1750_RESET);
  bool ok3 = bh1750WriteCmd(BH1750_CONT_HR);
  return ok1 && ok2 && ok3;
}
bool bh1750ReadLux(float& lux) {
  if (bhWire.requestFrom((uint8_t)BH1750_ADDR, (uint8_t)2) != 2) return false;
  uint16_t raw = ((uint16_t)bhWire.read() << 8) | bhWire.read();
  lux = ((float)raw) / 1.2f;
  return true;
}


// ============================================================
// SECCION 25: PANTALLA DE MODO AUTOMATICO
// Constantes de layout para las 3 tarjetas del modo auto
// (Luces, Ventilador, Cortina). Funciones para imprimir
// valores de sensores y estados en cada tarjeta en tiempo real.
// Los umbrales de lux controlan automaticamente luces y cortinas.
// ============================================================
static const int AUTO_TOP_TITLE_Y = 18;
static const int CARD_Y = 52;
static const int CARD_W = 96;
static const int CARD_H = 128;
static const int CARD_GAP = 8;
static const int CARD1_X = 10;
static const int CARD2_X = CARD1_X + CARD_W + CARD_GAP;
static const int CARD3_X = CARD2_X + CARD_W + CARD_GAP;
static const int VALUE_Y = CARD_Y + 94;
static const int LABEL_Y = VALUE_Y - 16;

static const int STATUS_Y = CARD_Y + 52;
static const int STATUS_H = 14;
static inline void clearValueArea(int x) {
  my_lcd.Set_Draw_color(THEME_CARD);
  my_lcd.Fill_Rectangle(x + 8, LABEL_Y - 4, x + CARD_W - 8, VALUE_Y + 20);
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Draw_Rectangle(x + 10, LABEL_Y - 6, x + CARD_W - 10, LABEL_Y - 6);
}

static inline void clearStatusArea(int x) {
  my_lcd.Set_Draw_color(THEME_CARD);
  my_lcd.Fill_Rectangle(x + 8, STATUS_Y, x + CARD_W - 8, STATUS_Y + STATUS_H);
}
static inline void printCenteredStatus(int x, const char* txt) {
  clearStatusArea(x);
  my_lcd.Set_Text_Back_colour(THEME_CARD);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Set_Text_Size(1);
  int tw = (int)strlen(txt) * 6;
  int tx = x + (CARD_W - tw) / 2;
  my_lcd.Print_String((char*)txt, tx, STATUS_Y + 3);
}

const float AUTO_LUX_ON = 80.0f;
const float AUTO_LUX_OFF = 150.0f;
const float AUTO_LUX_MAX_FOR_PERCENT = 1000.0f;

bool autoLucesOn = false;
bool autoCortinaAbierta = false;

static unsigned long lastAutoTick = 0;
static uint8_t autoStep = 0;
static float lastLux = 0.0f;
static bool lastLuxOk = false;
static float lastTemp = 0.0f;
static bool lastTempOk = false;
static unsigned long lastLedLuxMs = 0;
static bool autoTempFilterInit = false;
static float autoTempFiltered = 0.0f;

int luxToPercent(float lux) {
  if (lux < 0.0f) lux = 0.0f;
  if (lux > AUTO_LUX_MAX_FOR_PERCENT) lux = AUTO_LUX_MAX_FOR_PERCENT;
  return (int)((lux * 100.0f / AUTO_LUX_MAX_FOR_PERCENT) + 0.5f);
}

bool readAutoTempStable(float& out) {
  if (!mlxReady) return false;

  float sum = 0.0f;
  uint8_t okCount = 0;
  for (uint8_t i = 0; i < 4; i++) {
    float t;
    if (mlxReadObjectC(t) && t > -20.0f && t < 80.0f) {
      sum += t;
      okCount++;
    }
    delay(5);
  }

  if (okCount == 0) return false;

  float avg = sum / okCount;

  if (!autoTempFilterInit) {
    autoTempFiltered = avg;
    autoTempFilterInit = true;
  } else {
    autoTempFiltered = (autoTempFiltered * 0.65f) + (avg * 0.35f);
  }

  out = autoTempFiltered;
  return true;
}

void autoPrintLucesStatus() {
  const int x = CARD1_X;
  const char* st = "--";

  if (lastLuxOk) {
    if (lastLux < AUTO_LUX_ON) st = "POCA LUZ";
    else st = "LUZ ESTABLE";
  }

  printCenteredStatus(x, st);
}
void autoPrintFanStatus() {
  const int x = CARD2_X;
  const char* st = "--";

  if (lastTempOk) {
    if (lastTemp > 35.0f) st = "CALOR EXTREMO";
    else st = "TEMP. ESTABLE";
  }

  printCenteredStatus(x, st);
}
void autoPrintCurtainStatus() {
  const int x = CARD3_X;
  const char* st;
  if (curtainJog == JOG_OPEN) st = "ABRIENDO";
  else if (curtainJog == JOG_CLOSE) st = "CERRANDO";
  else st = (curtainState == OPEN) ? "ABIERTA" : "CERRADA";
  printCenteredStatus(x, st);
}
void autoPrintVentTemp(float tC, bool valid) {
  const int x = CARD2_X;
  clearValueArea(x);
  my_lcd.Set_Text_Back_colour(THEME_CARD);
  my_lcd.Set_Text_Size(1);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("Temperatura", x + 10, LABEL_Y);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  if (valid) {
    char b[12];
    dtostrf(tC, 4, 1, b);
    my_lcd.Print_String(b, x + 10, VALUE_Y);
    my_lcd.Set_Text_Size(1);
    my_lcd.Set_Text_colour(THEME_TEXT_ALT);
    my_lcd.Print_String(" C", x + 58, VALUE_Y + 4);
  } else {
    my_lcd.Print_String("--.-", x + 10, VALUE_Y);
    my_lcd.Set_Text_Size(1);
    my_lcd.Set_Text_colour(THEME_TEXT_ALT);
    my_lcd.Print_String(" C", x + 58, VALUE_Y + 4);
  }
}
void autoPrintLuxForLuces(float lux, bool ok) {
  const int x = CARD1_X;
  clearValueArea(x);
  my_lcd.Set_Text_Back_colour(THEME_CARD);
  my_lcd.Set_Text_Size(1);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("Luz", x + 26, LABEL_Y);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  if (ok) {
    char b[10];
    snprintf(b, sizeof(b), "%d%%", luxToPercent(lux));
    my_lcd.Print_String(b, x + 18, VALUE_Y);
  } else {
    my_lcd.Print_String("--%", x + 18, VALUE_Y);
  }
}
void autoPrintLuxForCurtain(float lux, bool ok) {
  const int x = CARD3_X;
  clearValueArea(x);
  my_lcd.Set_Text_Back_colour(THEME_CARD);
  my_lcd.Set_Text_Size(1);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("Luz", x + 26, LABEL_Y);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  if (ok) {
    char b[10];
    snprintf(b, sizeof(b), "%d%%", luxToPercent(lux));
    my_lcd.Print_String(b, x + 18, VALUE_Y);
  } else {
    my_lcd.Print_String("--%", x + 18, VALUE_Y);
  }
}

void drawAutoMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("Sistema automatico", 70, AUTO_TOP_TITLE_Y);
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Draw_Rectangle(14, AUTO_TOP_TITLE_Y + 18, 306, AUTO_TOP_TITLE_Y + 18);

  drawCard(CARD1_X, CARD_Y, CARD_W, CARD_H, "Luces");
  iconBulbSmall(CARD1_X + CARD_W / 2, CARD_Y + 36);

  drawCard(CARD2_X, CARD_Y, CARD_W, CARD_H, "Ventilador");
  iconFanSmall(CARD2_X + CARD_W / 2, CARD_Y + 36);

  drawCard(CARD3_X, CARD_Y, CARD_W, CARD_H, "Cortina");
  iconCurtainCompact(CARD3_X, CARD_Y, CARD_W);

  autoPrintLucesStatus();
  autoPrintFanStatus();
  autoPrintCurtainStatus();

  autoPrintLuxForLuces(0, false);
  autoPrintVentTemp(0.0, false);
  autoPrintLuxForCurtain(0, false);

  autoLucesOn = (ledDuty > 10);
  autoCortinaAbierta = (curtainState == OPEN);
  drawButton(110, 190, 100, 40, "VOLVER", THEME_PRIMARY);
}


// ============================================================
// SECCION 26: HELPERS DE DETECCION TACTIL
// Funciones para verificar si un toque cae dentro de un boton,
// con o sin margen de tolerancia extra.
// ============================================================
bool touchIn(int tx, int ty, int bx, int by, int bw, int bh, int margin = 10) {
  int left = bx - margin, right = bx + bw + margin, top = by - margin, bottom = by + bh + margin;
  return (tx >= left && tx <= right && ty >= top && ty <= bottom);
}
bool touchInExact(int tx, int ty, int bx, int by, int bw, int bh) {
  return (tx >= bx && tx <= bx + bw && ty >= by && ty <= by + bh);
}

bool getTouch(int& x, int& y) {
  TSPoint p = ts.getPoint();
  if (p.z <= 20) {
    TSPoint p2 = ts.getPoint();
    if (p2.z > p.z) p = p2;
  }
  pinMode(YP, OUTPUT);
  pinMode(XM, OUTPUT);
  digitalWrite(YP, HIGH);
  digitalWrite(XM, HIGH);

  if (p.z > 20 && p.z < 1000) {
    if (SWAPXY) {
      int t = p.x;
      p.x = p.y;
      p.y = t;
    }

    int tx = map(p.x, TS_LEFT, TS_RT, 0, my_lcd.Get_Display_Width() - 1);
    int ty = map(p.y, TS_TOP, TS_BOT, 0, my_lcd.Get_Display_Height() - 1);

    tx = constrain(tx, 0, my_lcd.Get_Display_Width() - 1);
    ty = constrain(ty, 0, my_lcd.Get_Display_Height() - 1);

    x = tx;
    y = my_lcd.Get_Display_Height() - 1 - ty;

    return true;
  }
  return false;
}


// ============================================================
// SECCION 27: CONTROL REMOTO INFRARROJO (IR)
// Recibe y decodifica senales del control remoto IR para
// controlar la TV (encendido, canal +/-, volumen +/-).
// ============================================================
#define IR_RX_PIN 31
#define IR_CODE_POWER 0x45
#define IR_CODE_CHM 0x44
#define IR_CODE_CHP 0x40
#define IR_CODE_VOLM 0x07
#define IR_CODE_VOLP 0x15
void handleIR() {
  if (IrReceiver.decode()) {
    uint8_t cmd = IrReceiver.decodedIRData.command;
    if (cmd == IR_CODE_POWER) {
      tvPower = !tvPower;
      tvFrame = 0; tvAnimLastMs = millis();
      tvRenderOLED();
    } else if (cmd == IR_CODE_CHP) {
      tvChannel = (tvChannel < 4) ? tvChannel + 1 : 1;
      tvFrame = 0; tvAnimLastMs = millis();
      tvRenderOLED();
    } else if (cmd == IR_CODE_CHM) {
      tvChannel = (tvChannel > 1) ? tvChannel - 1 : 4;
      tvFrame = 0; tvAnimLastMs = millis();
      tvRenderOLED();
    } else if (cmd == IR_CODE_VOLP) {
      if (tvVolLevel < TV_VOL_MAX) tvVolLevel++;
      tvRenderOLED();
    } else if (cmd == IR_CODE_VOLM) {
      if (tvVolLevel > TV_VOL_MIN) tvVolLevel--;
      tvRenderOLED();
    }
    IrReceiver.resume();
  }
}


// ============================================================
// SECCION 28: ALARMA
// Activa/desactiva una alarma en el pin definido. Puede
// dispararse por intentos fallidos de PIN. Se apaga sola
// tras 5 segundos usando serviceAlarm() en el loop.
// ============================================================
#define ALARM_PIN 41
#define ALARM_ACTIVE_LOW 0
static bool alarmaActiva = false;
static unsigned long alarmaHastaMs = 0;
inline void alarmOn() {
  digitalWrite(ALARM_PIN, ALARM_ACTIVE_LOW ? LOW : HIGH);
  alarmaActiva = true;
}
inline void alarmOff() {
  digitalWrite(ALARM_PIN, ALARM_ACTIVE_LOW ? HIGH : LOW);
  alarmaActiva = false;
  alarmaHastaMs = 0;
}
void triggerAlarm5s() {
  alarmaActiva = true;
  alarmaHastaMs = millis() + 5000UL;
  alarmOn();
}
void serviceAlarm() {
  if (alarmaActiva && millis() > alarmaHastaMs) { alarmOff(); }
}


// ============================================================
// SECCION 29: SISTEMA DE PIN DE SEGURIDAD Y TECLADO TACTIL
// Implementa un teclado numerico en pantalla para ingresar
// un PIN de 4 digitos que controla la cerradura (servo puerta).
// Tras 3 fallos consecutivos activa la alarma 5 segundos.
// ============================================================
const char PIN_CORRECTO[] = "1234";
static char pinBuf[5] = "";
static uint8_t pinLen = 0;
static uint8_t pinFallos = 0;
static bool tecladoVisible = false;
enum PinContext { PIN_CTX_AUTH, PIN_CTX_DISARM };
static PinContext pinCtx = PIN_CTX_AUTH;
enum SecResult { SEC_IDLE, SEC_PERMITIDO, SEC_DENEGADO, SEC_ESPERANDO };
SecResult secState = SEC_IDLE;
static const int PIN_BAR_X = 20, PIN_BAR_Y = 52, PIN_BAR_W = 200, PIN_BAR_H = 20;
static const int KP_BW = 50, KP_BH = 36, KP_GX = 6, KP_GY = 8;
static const int KP_BX = (320 - (4 * KP_BW + 3 * KP_GX + 52)) / 2;
static const int KP_BY = 100;
static const int OFF_W = 36, OFF_H = KP_BH, OFF_GAP_X = 16;
static const int OFF_X = KP_BX + 4 * (KP_BW + KP_GX) + OFF_GAP_X;
static const int OFF_Y = KP_BY + 0 * (KP_BH + KP_GY);
static const int BACK_W = 64, BACK_H = 32, BACK_MARGIN = 10;
static const int BACK_X = 320 - BACK_MARGIN - BACK_W;
static const int BACK_Y = 240 - BACK_MARGIN - BACK_H;
inline void pinClear() { pinLen = 0; pinBuf[0] = '\0'; }
inline void pinPush(char d) {
  if (pinLen < 4) { pinBuf[pinLen++] = d; pinBuf[pinLen] = '\0'; }
}
inline void pinBackspace() {
  if (pinLen > 0) { pinLen--; pinBuf[pinLen] = '\0'; }
}
void drawPinBar(const char* status = nullptr) {
  my_lcd.Set_Draw_color(THEME_CARD);
  my_lcd.Fill_Rectangle(PIN_BAR_X, PIN_BAR_Y, PIN_BAR_X + PIN_BAR_W, PIN_BAR_Y + PIN_BAR_H);
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Draw_Rectangle(PIN_BAR_X - 1, PIN_BAR_Y - 1, PIN_BAR_X + PIN_BAR_W + 1, PIN_BAR_Y + PIN_BAR_H + 1);
  my_lcd.Set_Text_Back_colour(THEME_CARD);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Set_Text_Size(1);
  my_lcd.Print_String("PIN:", PIN_BAR_X + 4, PIN_BAR_Y + 6);
  my_lcd.Print_String((char*)pinBuf, PIN_BAR_X + 34, PIN_BAR_Y + 6);
  char right[16];
  if (status) { strncpy(right, status, sizeof(right)); right[sizeof(right) - 1] = '\0'; }
  else { strncpy(right, puertaAbierta ? "ABIERTO" : "CERRADO", sizeof(right)); right[sizeof(right) - 1] = '\0'; }
  int sw = strlen(right) * 6;
  int sx = PIN_BAR_X + PIN_BAR_W - sw - 6;
  if (sx < PIN_BAR_X + 100) sx = PIN_BAR_X + 100;
  my_lcd.Print_String(right, sx, PIN_BAR_Y + 6);
}
inline void refreshPinBar(const char* status = nullptr) { drawPinBar(status); }

void drawPinPad() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Set_Text_Size(2);
  my_lcd.Print_String("Codigo de ingreso", 70, 20);
  drawPinBar("LISTO");
  drawButton(KP_BX + 0 * (KP_BW + KP_GX), KP_BY + 0 * (KP_BH + KP_GY), KP_BW, KP_BH, "1", THEME_PRIMARY);
  drawButton(KP_BX + 1 * (KP_BW + KP_GX), KP_BY + 0 * (KP_BH + KP_GY), KP_BW, KP_BH, "2", THEME_PRIMARY);
  drawButton(KP_BX + 2 * (KP_BW + KP_GX), KP_BY + 0 * (KP_BH + KP_GY), KP_BW, KP_BH, "3", THEME_PRIMARY);
  drawButton(KP_BX + 3 * (KP_BW + KP_GX), KP_BY + 0 * (KP_BH + KP_GY), KP_BW, KP_BH, "0", THEME_PRIMARY);
  drawButton(KP_BX + 0 * (KP_BW + KP_GX), KP_BY + 1 * (KP_BH + KP_GY), KP_BW, KP_BH, "4", THEME_PRIMARY);
  drawButton(KP_BX + 1 * (KP_BW + KP_GX), KP_BY + 1 * (KP_BH + KP_GY), KP_BW, KP_BH, "5", THEME_PRIMARY);
  drawButton(KP_BX + 2 * (KP_BW + KP_GX), KP_BY + 1 * (KP_BH + KP_GY), KP_BW, KP_BH, "6", THEME_PRIMARY);
  drawButton(KP_BX + 3 * (KP_BW + KP_GX), KP_BY + 1 * (KP_BH + KP_GY), KP_BW, KP_BH, "LISTO", THEME_PRIMARY);
  drawButton(KP_BX + 0 * (KP_BW + KP_GX), KP_BY + 2 * (KP_BH + KP_GY), KP_BW, KP_BH, "7", THEME_PRIMARY);
  drawButton(KP_BX + 1 * (KP_BW + KP_GX), KP_BY + 2 * (KP_BH + KP_GY), KP_BW, KP_BH, "8", THEME_PRIMARY);
  drawButton(KP_BX + 2 * (KP_BW + KP_GX), KP_BY + 2 * (KP_BH + KP_GY), KP_BW, KP_BH, "9", THEME_PRIMARY);
  drawButton(KP_BX + 3 * (KP_BW + KP_GX), KP_BY + 2 * (KP_BH + KP_GY), KP_BW, KP_BH, "<-", THEME_PRIMARY);
  drawButton(OFF_X, OFF_Y, OFF_W, OFF_H, "FIN", THEME_PRIMARY);
  drawButton(BACK_X, BACK_Y, BACK_W, BACK_H, "VOLVER", THEME_PRIMARY);
}
void pinSubmit() {
  if (keypadBloqueado) { refreshPinBar("BLOQ"); return; }
  if (pinLen != 4) { refreshPinBar("4DIG"); return; }
  if (strcmp(pinBuf, PIN_CORRECTO) == 0) {
    pinFallos = 0;
    refreshPinBar("ACEPTADO");
    keypadBloqueado = true;
    if (!puertaAbierta) { moverServoA(PUERTA_ABIERTA); puertaAbierta = true; }
  } else {
    pinFallos++;
    refreshPinBar("INC");
    if (pinFallos >= 3 && !alarmaActiva) { triggerAlarm5s(); refreshPinBar("ALARMA"); }
  }
  pinClear();
}
void handlePinPadTouch(int tx, int ty) {
  if (!tecladoVisible) return;
  if (touchInExact(tx, ty, BACK_X, BACK_Y, BACK_W, BACK_H)) { tecladoVisible = false; drawSecurityMenu(); return; }
  if (touchInExact(tx, ty, OFF_X, OFF_Y, OFF_W, OFF_H)) {
    alarmOff();
    if (puertaAbierta) { moverServoA(PUERTA_CERRADA); puertaAbierta = false; }
    keypadBloqueado = false;
    pinClear();
    refreshPinBar("FIN");
    return;
  }
  if (keypadBloqueado) return;
  auto hit = [&](int cx, int cy) -> bool {
    return touchInExact(tx, ty, KP_BX + cx * (KP_BW + KP_GX), KP_BY + cy * (KP_BH + KP_GY), KP_BW, KP_BH);
  };
  if (hit(0, 0)) pinPush('1');
  else if (hit(1, 0)) pinPush('2');
  else if (hit(2, 0)) pinPush('3');
  else if (hit(3, 0)) pinSubmit();
  else if (hit(0, 1)) pinPush('4');
  else if (hit(1, 1)) pinPush('5');
  else if (hit(2, 1)) pinPush('6');
  else if (hit(3, 1)) pinSubmit();
  else if (hit(0, 2)) pinPush('7');
  else if (hit(1, 2)) pinPush('8');
  else if (hit(2, 2)) pinPush('9');
  else if (hit(3, 2)) pinBackspace();
  refreshPinBar();
}


// ============================================================
// SECCION 30: MENU DE CORTINAS Y ULTRASONIDO
// Dibuja los menus de cortinas (botones IZQ/DER) y el de
// ultrasonido (solo informativo). Incluye funcion para
// actualizar el estado visible de las cortinas en pantalla.
// ============================================================
void updateCurtainStatus(const char* forced) {
  if (!uiAllowOverlays()) return;
  if (currentMenu != 7) return;
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(22, 192, 298, 218);
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Draw_Rectangle(20, 190, 300, 190);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  const char* msg = forced;
  if (!msg) {
    if (curtainState == OPEN) msg = "Abierta";
    else if (curtainState == CLOSED) msg = "Cerrada";
    else if (curtainState == MOVING_OPEN) msg = "Abriendo...";
    else msg = "Cerrando...";
  }
  my_lcd.Print_String((char*)msg, 120, 200);
}
void drawCurtainsMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("CORTINAS", 110, 20);
  drawButton(40, 70, 100, 60, "IZQ <", THEME_PRIMARY);
  drawButton(180, 70, 100, 60, "> DER", THEME_PRIMARY);
  drawButton(110, 150, 100, 40, "VOLVER", THEME_PRIMARY);
  updateCurtainStatus(nullptr);
}

void drawUltrasonicMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Print_String("ULTRASONIDO", 100, 20);
  drawButton(110, 190, 100, 40, "VOLVER", THEME_PRIMARY);
}


// ============================================================
// SECCION 31: MENU DE SEGURIDAD, RFID Y PANTALLA DE ESTADO
// Dibuja el menu de puerta con acceso por PIN o RFID.
// Gestiona la lectura del lector RFID RC522, compara el UID
// contra los autorizados, abre la puerta si coincide y la
// cierra automaticamente tras 10 segundos.
// ============================================================
void drawSecurityMenu() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_ALT);
  my_lcd.Print_String("PUERTA", 110, 20);
  drawButton(20, 80, 100, 40, "PIN", THEME_PRIMARY);
  drawButton(200, 80, 100, 40, "RFID", THEME_PRIMARY);
  drawButton(110, 190, 100, 40, "VOLVER", THEME_PRIMARY);
  my_lcd.Set_Draw_color(THEME_CARD_BDR);
  my_lcd.Draw_Rectangle(20, 130, 300, 160);
  secState = SEC_IDLE;
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Print_String("Estado:", 30, 136);
  my_lcd.Print_String("LISTO", 120, 136);
}
void drawRFIDScreen() {
  drawGradientV(THEME_BG_TOP, THEME_BG_BOTTOM);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Print_String("RFID", 150, 20);
  my_lcd.Set_Text_Size(2);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Print_String("Acerque la tarjeta", 70, 90);
  drawButton(110, 190, 100, 40, "VOLVER", THEME_PRIMARY);
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(30, 130, 300, 170);
  rfidDrawnOnceInRFID = false;
}
void rfidDrawStatusInSecurity() {
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(20, 146, 300, 160);
  my_lcd.Set_Text_Size(1);
  my_lcd.Print_String("RFID:", 30, 150);
  const char* msg = rfidSeen ? (rfidAccepted ? "ACEPTADO" : "RECHAZADO") : "--";
  my_lcd.Print_String((char*)msg, 80, 150);
  rfidDrawnOnceInSec = true;
}
void rfidDrawStatusInRFID() {
  my_lcd.Set_Text_Back_colour(THEME_BG_TOP);
  my_lcd.Set_Text_colour(THEME_TEXT_DARK);
  my_lcd.Set_Draw_color(THEME_BG_TOP);
  my_lcd.Fill_Rectangle(30, 130, 300, 170);
  my_lcd.Set_Text_Size(2);
  const char* big = rfidSeen ? (rfidAccepted ? "ACEPTADO" : "RECHAZADO") : "--";
  my_lcd.Print_String((char*)big, 90, 130);
  my_lcd.Set_Text_Size(1);
  if (rfidSeen) {
    my_lcd.Print_String("UID:", 30, 170);
    my_lcd.Print_String(rfidUidStr, 60, 170);
  }
  rfidDrawnOnceInRFID = true;
}
static inline void rfidUidToStringUpper(char* out, size_t outSz) {
  out[0] = '\0';
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    byte b = mfrc522.uid.uidByte[i];
    char tmp[3];
    snprintf(tmp, sizeof(tmp), "%02X", b);
    strncat(out, tmp, outSz - 1);
  }
  out[outSz - 1] = '\0';
}
static inline bool rfidIsAccepted(const char* uidHex) {
  for (byte i = 0; i < NUM_UIDS_OK; i++) {
    if (strcmp(uidHex, UIDS_ACEPTADOS[i]) == 0) return true;
  }
  return false;
}
void rfidServiceAndUI() {
  bool updated = false;
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    rfidUidToStringUpper(rfidUidStr, sizeof(rfidUidStr));
    rfidAccepted = rfidIsAccepted(rfidUidStr);
    rfidSeen = true;
    if (rfidAccepted) {
      if (!puertaAbierta) { moverServoA(PUERTA_ABIERTA); puertaAbierta = true; }
      rfidOpenedDoor = true;
      rfidDoorCloseAtMs = millis() + 10000UL;
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    updated = true;
  }
  if (currentMenu == 6) {
    if (!rfidDrawnOnceInSec || updated) rfidDrawStatusInSecurity();
  } else {
    rfidDrawnOnceInSec = false;
  }
  if (currentMenu == 9) {
    if (!rfidDrawnOnceInRFID || updated) rfidDrawStatusInRFID();
  } else {
    rfidDrawnOnceInRFID = false;
  }
}


// ============================================================
// SECCION 32: COMUNICACION WEBSOCKET (Serial1 / ESP32)
// Recibe comandos de texto por Serial1 desde un ESP32 bridge
// WebSocket. Interpreta y ejecuta comandos como LED, FAN,
// GARAGE, CURTAIN, DOOR, ALARM, TV, PIN, STATE?, SENSE?, etc.
// Responde con ACK/ERR y envia el estado completo del sistema.
// ============================================================
void wsPrintln(const String& s) { Serial1.println(s); }
void wsAck(const char* what) { Serial1.print(F("ACK ")); Serial1.println(what); }
void wsErr(const char* what) { Serial1.print(F("ERR ")); Serial1.println(what); }

float readTempC_() {
  float t;
  if (mlxReady && mlxReadObjectC(t)) return t;
  return NAN;
}
float readLux_() {
  float lx;
  if (bhReady && bh1750ReadLux(lx)) return lx;
  return NAN;
}
long readDist_() { return readUltrasonic(); }

const char* curtainStateStr() {
  switch (curtainState) {
    case OPEN: return "OPEN";
    case CLOSED: return "CLOSED";
    default: return (curtainJog == JOG_OPEN ? "OPENING" : (curtainJog == JOG_CLOSE ? "CLOSING" : "--"));
  }
}
const char* tvOnOff() { return tvPower ? "ON" : "OFF"; }

void sendSTATE() {
  float lx = readLux_();
  float tc = readTempC_();
  long d = readDist_();
  Serial1.print(F("STATE "));
  Serial1.print(F("L=")); Serial1.print(ledDuty);
  Serial1.print(F(";F=")); Serial1.print(fanLevel);
  Serial1.print(F(";LUX=")); if (isnan(lx)) Serial1.print(F("--")); else Serial1.print(lx, 1);
  Serial1.print(F(";TC=")); if (isnan(tc)) Serial1.print(F("--")); else Serial1.print(tc, 1);
  Serial1.print(F(";CURT=")); Serial1.print(curtainStateStr());
  Serial1.print(F(";TV=")); Serial1.print(tvOnOff());
  Serial1.print(F(";VOL=")); Serial1.print(tvVolLevel);
  Serial1.print(F(";DIST=")); if (d > 0) Serial1.println(d); else Serial1.println(F("--"));
}
void sendSENSE() { sendSTATE(); }

bool handlePinOpenFromCmd(const String& sUpper) {
  if (!sUpper.startsWith("PIN ")) return false;
  String p = sUpper.substring(4);
  p.trim();
  if (p.length() != 4) { wsErr("PIN_LEN"); return true; }
  if (p.equals(String(PIN_CORRECTO))) {
    if (!puertaAbierta) { moverServoA(PUERTA_ABIERTA); puertaAbierta = true; }
    keypadBloqueado = true;
    wsAck("PIN_OK");
  } else {
    wsErr("PIN_BAD");
  }
  sendSTATE();
  return true;
}

void handleWsCommand(const String& ln) {
  uiCmdSource = UI_SRC_APP;

  String s = ln;
  s.trim();
  String sUpper = s;
  sUpper.toUpperCase();

  if (s.startsWith("IP ") || s.startsWith("ip ") || s.startsWith("Ip ")) {
    wsIp = s.substring(3);
    wsIp.trim();
    if (currentMenu == 0) { drawIpLabel(); }
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }

  if (sUpper == "STATE?") { sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "SENSE?") { sendSENSE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "DIST?") { long d = readDist_(); wsPrintln(String("DIST=") + (d > 0 ? String(d) : "--")); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "LUX?") { float lx = readLux_(); wsPrintln(String("LUX=") + (isnan(lx) ? "--" : String(lx, 1))); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "TEMP?" || sUpper == "TC?") { float tc = readTempC_(); wsPrintln(String("TC=") + (isnan(tc) ? "--" : String(tc, 1))); uiCmdSource = UI_SRC_LOCAL; return; }

  if (handlePinOpenFromCmd(sUpper)) { uiCmdSource = UI_SRC_LOCAL; return; }

  if (sUpper.startsWith("LED ")) {
    int v = sUpper.substring(4).toInt();
    v = constrain(v, 0, 255);
    setLedDuty((uint8_t)v);
    wsAck("LED");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }

  if (sUpper.startsWith("FAN ")) {
    int f = sUpper.substring(4).toInt();
    f = constrain(f, 0, 3);
    setFanSpeed(f);
    wsAck("FAN");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }

  if (sUpper == "GARAGE OPEN") {
    garageStartOpen();
    wsAck("GARAGE OPEN");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }
  if (sUpper == "GARAGE CLOSE") {
    garageStartClose();
    wsAck("GARAGE CLOSE");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }

  if (sUpper == "CURTAIN OPEN") {
    curtainJogStartOpen();
    wsAck("CURTAIN OPEN");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }
  if (sUpper == "CURTAIN CLOSE") {
    curtainJogStartClose();
    wsAck("CURTAIN CLOSE");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }

  if (sUpper == "ALARM ON" || sUpper == "ALARM_ON") { alarmOn(); wsAck("ALARM_ON"); sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "ALARM OFF" || sUpper == "ALARM_OFF") { alarmOff(); wsAck("ALARM_OFF"); sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "LOCK") {
    if (puertaAbierta) { moverServoA(PUERTA_CERRADA); puertaAbierta = false; }
    keypadBloqueado = false;
    wsAck("LOCK");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }
  if (sUpper == "UNLOCK") {
    if (!puertaAbierta) { moverServoA(PUERTA_ABIERTA); puertaAbierta = true; }
    keypadBloqueado = true;
    wsAck("UNLOCK");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }

  if (sUpper == "DOOR OPEN") {
    if (!puertaAbierta) { moverServoA(PUERTA_ABIERTA); puertaAbierta = true; }
    wsAck("DOOR OPEN");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }
  if (sUpper == "DOOR CLOSE") {
    if (puertaAbierta) { moverServoA(PUERTA_CERRADA); puertaAbierta = false; }
    wsAck("DOOR CLOSE");
    sendSTATE();
    uiCmdSource = UI_SRC_LOCAL;
    return;
  }

  if (sUpper == "TV POWER") { tvPower = !tvPower; tvRenderOLED(); wsAck("TVPWR"); sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "TV VOL+") { if (tvVolLevel < TV_VOL_MAX) tvVolLevel++; tvRenderOLED(); wsAck("TV VOL+"); sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "TV VOL-") { if (tvVolLevel > TV_VOL_MIN) tvVolLevel--; tvRenderOLED(); wsAck("TV VOL-"); sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "TV CH+") { tvChannel = (tvChannel < TV_CHANNELS) ? tvChannel + 1 : 1; tvRenderOLED(); wsAck("TV CH+"); sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }
  if (sUpper == "TV CH-") { tvChannel = (tvChannel > 1) ? tvChannel - 1 : TV_CHANNELS; tvRenderOLED(); wsAck("TV CH-"); sendSTATE(); uiCmdSource = UI_SRC_LOCAL; return; }

  wsErr("UNKNOWN");
  uiCmdSource = UI_SRC_LOCAL;
}
void wsSerial1Service() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\r') continue;
    if (c == '\n') {
      if (_wsLine.length()) {
        handleWsCommand(_wsLine);
        _wsLine = "";
      }
    } else {
      if (_wsLine.length() < 160) _wsLine += c;
    }
  }
}


// ============================================================
// SECCION 33: SETUP - INICIALIZACION DEL SISTEMA
// Inicializa en orden: Serial, LCD, pines de salida, PWM,
// microfono, alarma, servos (cortina, garaje, puerta),
// IR, OLED/TV, sensores MLX90614 y BH1750, RFID, Serial1
// y dibuja el menu principal al terminar.
// ============================================================
void setup() {
  Serial.begin(115200);
  while (!Serial) { ; }

  my_lcd.Init_LCD();
  my_lcd.Set_Rotation(1);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_PWM_PIN, OUTPUT);
  pinMode(FAN_IN1_PIN, OUTPUT);
  pinMode(FAN_IN2_PIN, OUTPUT);
  fanDirForward();
  pinMode(FAN_PWM_PIN, OUTPUT);

  setLedDuty(0);
  setFanDuty(0);
  pinMode(MIC_PIN, INPUT);
  pinMode(ALARM_PIN, OUTPUT);
  alarmOff();

  curt_servo_begin(CURTAIN_SERVO_PIN);
  curtainServoStop();
  delay(100);
  curtainHomeOnBoot();

  gar_servo_begin(GARAGE_SERVO_PIN);
  garageServoStop();
  garageState = GARAGE_CLOSED;
  garageCycleActive = false;

  IrReceiver.begin(IR_RX_PIN, ENABLE_LED_FEEDBACK);

  servo_begin(SERVO_PIN);
  servo_write_angle(PUERTA_CERRADA);
  puertaAbierta = false;
  keypadBloqueado = false;

  u8g2.begin();
  tvRenderOLED();

  mlxWire.begin();
  delay(50);
  {
    float t;
    mlxReady = mlxReadObjectC(t);
  }
  bhWire.begin();
  bhReady = bh1750Begin();

  SPI.begin();
  mfrc522.PCD_Init(RFID_SS_PIN, RFID_RST_PIN);

  Serial1.begin(9600);

  drawMainMenu();
}


// ============================================================
// SECCION 34: LOOP PRINCIPAL
// Ciclo principal del sistema. En cada iteracion:
// - Atiende comandos WebSocket entrantes por Serial1.
// - Detecta y procesa toques en la pantalla con debounce.
// - Navega entre menus segun el toque y el menu activo.
// - Actualiza el IR, la animacion OLED de TV y el garaje.
// - En modo automatico: lee sensores y ajusta luces/ventilador/cortina.
// - En menu de luces: actualiza el valor de lux periodicamente.
// - Detecta aplausos, gestiona la alarma y el autocierre RFID.
// - Atiende el servicio de cortinas y RFID continuamente.
// ============================================================
void loop() {
  wsSerial1Service();

  int x, y;
  bool touched = getTouch(x, y);

  if (touched) {
    unsigned long now = millis();
    if (now - uiLastTouchMs >= UI_TOUCH_DEBOUNCE_MS) {
      uiLastTouchMs = now;

      uiCmdSource = UI_SRC_LOCAL;
      uiBlockHeavyUntilMs = now + UI_HEAVY_BLOCK_MS;

      if (tecladoVisible) {
        handlePinPadTouch(x, y);
        return;
      }

      if (currentMenu == 10) {
        if (touchIn(x, y, 110, 190, 100, 40, 25)) {
          currentMenu = 0;
          drawMainMenu();
          return;
        }
      }

      if (currentMenu == 0) {
        if (touchIn(x, y, 110, 80, 100, 50)) { currentMenu = 1; drawSubMenu(); return; }
        else if (touchIn(x, y, 110, 140, 100, 50)) { currentMenu = 10; drawAutoMenu(); return; }
      } else if (currentMenu == 1) {
        if (touchIn(x, y, 20, 60, 80, 50)) { currentMenu = 4; drawGarageMenu(); return; }
        else if (touchIn(x, y, 120, 60, 80, 50)) { currentMenu = 3; drawLedMenu(); return; }
        else if (touchIn(x, y, 220, 60, 80, 50)) { currentMenu = 5; drawFanMenu(); return; }
        else if (touchIn(x, y, 20, 130, 80, 50)) { currentMenu = 6; drawSecurityMenu(); return; }
        else if (touchIn(x, y, 120, 130, 80, 50)) { currentMenu = 7; drawCurtainsMenu(); return; }
        else if (touchIn(x, y, 220, 130, 80, 50)) { currentMenu = 8; drawTVMenu(); return; }
        else if (touchIn(x, y, 110, 190, 100, 40)) { currentMenu = 0; drawMainMenu(); return; }
      } else if (currentMenu == 3) {
        handleLedTouch(x, y);
        if (touchIn(x, y, 20, 200, 80, 40)) { setLedDuty(0); drawLedSliderKnob(ledDuty); drawLedPercent(ledDuty); return; }
        if (touchIn(x, y, 110, 200, 100, 40)) { currentMenu = 1; drawSubMenu(); return; }
      } else if (currentMenu == 4) {
        bool hitOpen = touchInExact(x, y, 40, 90, 100, 60);
        bool hitClose = touchInExact(x, y, 180, 90, 100, 60);
        if (hitOpen) { garageStartOpen(); return; }
        else if (hitClose) { garageStartClose(); return; }
        else if (touchIn(x, y, 110, 170, 100, 40)) {
          if (garageState == GARAGE_OPENING || garageState == GARAGE_CLOSING || garageState == GARAGE_PAUSED_OBS) {
            garageStopAll();
            garageCycleActive = false;
          }
          currentMenu = 1;
          drawSubMenu();
          return;
        }
      } else if (currentMenu == 5) {
        if (touchIn(x, y, 20, 90, 80, 50)) { setFanSpeed(1); return; }
        else if (touchIn(x, y, 120, 90, 80, 50)) { setFanSpeed(2); return; }
        else if (touchIn(x, y, 220, 90, 80, 50)) { setFanSpeed(3); return; }
        else if (touchIn(x, y, 50, 160, 80, 50)) { setFanSpeed(0); return; }
        else if (touchIn(x, y, 180, 160, 80, 50)) { currentMenu = 1; drawSubMenu(); return; }
      } else if (currentMenu == 6) {
        if (touchIn(x, y, 20, 80, 100, 40)) { pinCtx = PIN_CTX_AUTH; pinClear(); tecladoVisible = true; drawPinPad(); return; }
        else if (touchIn(x, y, 200, 80, 100, 40)) { currentMenu = 9; drawRFIDScreen(); return; }
        else if (touchIn(x, y, 110, 190, 100, 40)) { currentMenu = 1; drawSubMenu(); return; }
      } else if (currentMenu == 7) {
        bool inLeft = touchInExact(x, y, 40, 70, 100, 60);
        bool inRight = touchInExact(x, y, 180, 70, 100, 60);
        if (inLeft) { curtainJogStartOpen(); curtainLastTouchMs = millis(); return; }
        else if (inRight) { curtainJogStartClose(); curtainLastTouchMs = millis(); return; }
        else if (touchInExact(x, y, 110, 150, 100, 40)) { curtainJogStop(); currentMenu = 1; drawSubMenu(); return; }
        else { if (curtainJog != JOG_NONE) curtainJogStop(); return; }
      } else if (currentMenu == 8) {
        if (touchIn(x, y, 20, 60, 100, 50)) { tvPower = !tvPower; tvFrame = 0; tvAnimLastMs = millis(); tvRenderOLED(); return; }
        else if (touchIn(x, y, 20, 190, 100, 40)) { currentMenu = 1; drawSubMenu(); return; }
        else if (touchIn(x, y, 130, 60, 80, 40)) { tvChannel = (tvChannel > 1) ? tvChannel - 1 : 4; tvFrame = 0; tvAnimLastMs = millis(); tvRenderOLED(); return; }
        else if (touchIn(x, y, 220, 60, 80, 40)) { tvChannel = (tvChannel < 4) ? tvChannel + 1 : 1; tvFrame = 0; tvAnimLastMs = millis(); tvRenderOLED(); return; }
        else if (touchIn(x, y, 130, 110, 80, 40)) { if (tvVolLevel < TV_VOL_MAX && tvVolLevel > 0) tvVolLevel--; tvRenderOLED(); return; }
        else if (touchIn(x, y, 220, 110, 80, 40)) { if (tvVolLevel < TV_VOL_MAX) tvVolLevel++; tvRenderOLED(); return; }
      } else if (currentMenu == 9) {
        if (touchIn(x, y, 110, 190, 100, 40)) { currentMenu = 6; drawSecurityMenu(); return; }
      }
    }
  }

  if (currentMenu == 7 && curtainJog != JOG_NONE) {
    if (!touched && (millis() - curtainLastTouchMs > 80)) {
      uiCmdSource = UI_SRC_LOCAL;
      curtainJogStop();
    }
  }

  handleIR();
  tvServiceOLED();
  garageService();

  if (uiAllowHeavyWorkNow()) {
    unsigned long now = millis();

    if (currentMenu == 10) {
      if (now - uiLastTouchMs > 180) {
        if (now - lastAutoTick >= 220) {
          lastAutoTick = now;

          if (autoStep == 0) {
            lastTempOk = readAutoTempStable(lastTemp);
            autoPrintVentTemp(lastTempOk ? lastTemp : 0.0, lastTempOk);

            if (lastTempOk) {
              if (lastTemp > 35.0f) {
                setFanSpeed(3);
              } else {
                setFanSpeed(0);
              }
            }
          } else if (autoStep == 1) {
            lastLuxOk = bhReady && bh1750ReadLux(lastLux);
            autoPrintLuxForLuces(lastLuxOk ? lastLux : 0.0, lastLuxOk);

            if (lastLuxOk) {
              if (!autoLucesOn && lastLux < AUTO_LUX_ON) { setLedDuty(255); autoLucesOn = true; }
              else if (autoLucesOn && lastLux >= AUTO_LUX_ON) { setLedDuty(0); autoLucesOn = false; }
            }
          } else {
            autoPrintLuxForCurtain(lastLuxOk ? lastLux : 0.0, lastLuxOk);

            if (lastLuxOk) {
              uiCmdSource = UI_SRC_AUTO;
              if (!autoCortinaAbierta && lastLux < AUTO_LUX_ON) { curtainJogStartOpen(); autoCortinaAbierta = true; }
              else if (autoCortinaAbierta && lastLux > AUTO_LUX_OFF) { curtainJogStartClose(); autoCortinaAbierta = false; }
              uiCmdSource = UI_SRC_LOCAL;
            }

            autoPrintLucesStatus();
            autoPrintFanStatus();
            autoPrintCurtainStatus();
          }

          autoStep = (autoStep + 1) % 3;
        }
      }
    }

    if (currentMenu == 3) {
      if (now - uiLastTouchMs > 120) {
        if (now - lastLedLuxMs >= 450) {
          lastLedLuxMs = now;
          float lux;
          bool okLux = bhReady && bh1750ReadLux(lux);
          updateLuxInLedMenu(okLux ? lux : 0.0f, okLux);
        }
      }
    }
  }

  handleClapToggle();
  serviceAlarm();

  if (rfidOpenedDoor && puertaAbierta && rfidDoorCloseAtMs && millis() > rfidDoorCloseAtMs) {
    moverServoA(PUERTA_CERRADA);
    puertaAbierta = false;
    rfidOpenedDoor = false;
    rfidDoorCloseAtMs = 0;
    rfidSeen = false;
    rfidDrawnOnceInRFID = false;
    rfidDrawnOnceInSec = false;
    if (currentMenu == 9) drawRFIDScreen();
    else if (currentMenu == 6) rfidDrawStatusInSecurity();
  }

  rfidServiceAndUI();
  curtainService();
}
