#include <WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <FS.h>
using fs::FS;         // Correção para conflito de namespace WebServer no ESP32 3.x
#include <WebServer.h>
#include <DNSServer.h>

// Objeto de preferências para armazenamento persistente
Preferences preferences;

// Credenciais WiFi - serão substituídas pelos valores armazenados
String ssid = "";
String password = "";

// Configuração da API
String apiEndpoint = "";
String apiHost = "";
int apiPort = 80;     // Porta HTTP padrão

// Device ID e informações
String deviceId = "ESP32-001";  // ID único deste dispositivo
String deviceName = "Entrada Principal";  // Nome do dispositivo
String deviceLocation = "Receção";  // Localização física
String firmwareVersion = "1.0.0";

// Modo de vinculação de cartão
bool enrollmentMode = false;
String enrollmentCode = "";
String enrollmentUserName = "";
unsigned long enrollmentStartTime = 0;
const unsigned long enrollmentTimeout = 300000; // 5 minutos

// UIDs dos cartões de administrador - ADICIONE OS IDs DOS SEUS CARTÕES AQUI
const String ADMIN_CARDS[] = {
  "BB257506",  // Substitua pelo UID do seu cartão de administrador após lê-lo
  "PLACEHOLDER2",  // Pode adicionar múltiplos cartões de administrador
  "PLACEHOLDER3"
};
const int ADMIN_CARDS_COUNT = sizeof(ADMIN_CARDS) / sizeof(ADMIN_CARDS[0]);

// Servidor web para configuração WiFi
WebServer server(80);
DNSServer dnsServer;
const byte DNS_PORT = 53;

// Configurações do servidor NTP para Portugal
const char* ntpServer = "pt.pool.ntp.org";
const long gmtOffset_sec = 0;      // Portugal está em GMT+0 no inverno
const int daylightOffset_sec = 3600; // +1 hora para horário de verão

// Configuração I2C do PN532
#define PN532_SDA 25
#define PN532_SCL 26
Adafruit_PN532 nfc(PN532_SDA, PN532_SCL);

// Configuração do buzzer
int buzzerPin = 5; // GPIO 5 para o buzzer

// Configuração do ecrã TFT
TFT_eSPI tft = TFT_eSPI();

// Variáveis globais
bool wifiConnected = false;
bool wifiConfigMode = false;  // Verdadeiro quando em modo de configuração AP
bool wifiFailedMode = false;   // Verdadeiro quando a ligação WiFi falha
String currentTime = "00:00";
String previousTime = "";
bool wifiStatusChanged = false;
unsigned long lastTimeUpdate = 0;
const unsigned long timeUpdateInterval = 1000;

// Heartbeat variables
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 30000; // 30 segundos

// Enrollment polling variables
unsigned long lastEnrollmentCheck = 0;
const unsigned long enrollmentCheckInterval = 5000; // Check every 5 seconds

// Variáveis de animação
unsigned long lastFrameTime = 0;
int frameIndex = 0;
const int frameCount = 3;
const unsigned long frameDuration = 650;
bool animationDirection = true; // verdadeiro = avançar, falso = retroceder

// Variáveis do NFC e ecrã de boas-vindas
bool showWelcomeScreen = false;
String welcomeUsername = "";
String welcomeType = "";  // Adicionado para guardar o tipo (entrada/saída)
unsigned long welcomeStartTime = 0;
const unsigned long welcomeDisplayDuration = 5000; // 5 segundos
bool nfcInitialized = false;

static const unsigned char PROGMEM image_wifi_not_connected_bits[] = {
  0x0c,0x03,0xff,0x00,0x00,0x0c,0x03,0xff,0x00,0x00,0x03,0x3c,0x00,0xf0,0x00,
  0x03,0x3c,0x00,0xf0,0x00,0x00,0xc0,0x00,0x0f,0x00,0x00,0xc0,0x00,0x0f,0x00,
  0x0c,0x33,0xff,0x00,0xc0,0x0c,0x33,0xff,0x00,0xc0,0x30,0x0c,0x00,0xf0,0x30,
  0x30,0x0c,0x00,0xf0,0x30,0xc0,0xc3,0x00,0x0c,0x0c,0xc0,0xc3,0x00,0x0c,0x0c,
  0x03,0x00,0xcc,0x03,0x00,0x03,0x00,0xcc,0x03,0x00,0x0c,0x0f,0x33,0xc0,0xc0,
  0x0c,0x0f,0x33,0xc0,0xc0,0x00,0x30,0x0c,0x30,0x00,0x00,0x30,0x0c,0x30,0x00,
  0x00,0xc0,0x33,0x0c,0x00,0x00,0xc0,0x33,0x0c,0x00,0x00,0x03,0xcc,0xc0,0x00,
  0x00,0x03,0xcc,0xc0,0x00,0x00,0x0c,0x00,0x30,0x00,0x00,0x0c,0x00,0x30,0x00,
  0x00,0x00,0x30,0x0c,0x00,0x00,0x00,0x30,0x0c,0x00,0x00,0x00,0xcc,0x03,0x00,
  0x00,0x00,0xcc,0x03,0x00,0x00,0x00,0x30,0x00,0xc0,0x00,0x00,0x30,0x00,0xc0,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// Bitmap WiFi ligado (38x32) - Ícone WiFi simples
static const unsigned char PROGMEM image_wifi_connected_bits[] = {
  0x00,0x03,0xff,0x00,0x00,0x00,0x03,0xff,0x00,0x00,0x00,0x3f,0xff,0xf0,0x00,0x00,0x3f,0xff,0xf0,0x00,0x03,0xfc,0x00,0xff,0x00,0x03,0xfc,0x00,0xff,0x00,0x0f,0xc3,0xff,0x0f,0xc0,0x0f,0xc3,0xff,0x0f,0xc0,0x3f,0x3f,0xff,0xf3,0xf0,0x3f,0x3f,0xff,0xf3,0xf0,0xfc,0xff,0x03,0xfc,0xfc,0xfc,0xff,0x03,0xfc,0xfc,0x33,0xf0,0xfc,0x3f,0x30,0x33,0xf0,0xfc,0x3f,0x30,0x0f,0xcf,0xff,0xcf,0xc0,0x0f,0xcf,0xff,0xcf,0xc0,0x03,0x3f,0x03,0xf3,0x00,0x03,0x3f,0x03,0xf3,0x00,0x00,0xfc,0xfc,0xfc,0x00,0x00,0xfc,0xfc,0xfc,0x00,0x00,0x33,0xff,0x30,0x00,0x00,0x33,0xff,0x30,0x00,0x00,0x0f,0xcf,0xc0,0x00,0x00,0x0f,0xcf,0xc0,0x00,0x00,0x03,0x33,0x00,0x00,0x00,0x03,0x33,0x00,0x00,0x00,0x00,0xfc,0x00,0x00,0x00,0x00,0xfc,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};


static const uint8_t PROGMEM connect_button_frame2[] = { 
  0x00, 0x07, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xc0, 0x00, 0x00, 0x1f, 0xc0, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x0f, 0xe0, 0x00, 0x00, 0x1f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf0, 
	0x00, 0x00, 0x3f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xf8, 0x00, 0x00, 0x7f, 0xc0, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x0f, 0xf8, 0x00, 0x00, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 
	0xfc, 0x00, 0x01, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfe, 0x00, 0x01, 0xff, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0x00, 0x03, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x03, 0xff, 0x00, 0x07, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x80, 0x07, 0xfc, 
	0x00, 0x78, 0x00, 0x00, 0x00, 0x3c, 0x00, 0xff, 0x80, 0x0f, 0xf8, 0x01, 0xfc, 0x00, 0x00, 0x00, 
	0xfe, 0x00, 0x7f, 0xc0, 0x0f, 0xf0, 0x01, 0xfc, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x3f, 0xe0, 0x1f, 
	0xf0, 0x03, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x00, 0x3f, 0xe0, 0x1f, 0xe0, 0x07, 0xfc, 0x00, 0x00, 
	0x00, 0xff, 0x80, 0x1f, 0xf0, 0x3f, 0xe0, 0x07, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x80, 0x1f, 0xf0, 
	0x3f, 0xc0, 0x0f, 0xf8, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0x0f, 0xf0, 0x3f, 0xc0, 0x0f, 0xf8, 0x00, 
	0x00, 0x00, 0x7f, 0xc0, 0x0f, 0xf8, 0x7f, 0x80, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x07, 
	0xf8, 0x7f, 0x80, 0x1f, 0xe0, 0x00, 0x70, 0x00, 0x1f, 0xe0, 0x07, 0xf8, 0x7f, 0x80, 0x3f, 0xe0, 
	0x01, 0xfe, 0x00, 0x1f, 0xf0, 0x07, 0xfc, 0xff, 0x00, 0x3f, 0xc0, 0x07, 0xff, 0x80, 0x0f, 0xf0, 
	0x03, 0xfc, 0xff, 0x00, 0x3f, 0xc0, 0x0f, 0xff, 0xc0, 0x0f, 0xf0, 0x03, 0xfc, 0xff, 0x00, 0x3f, 
	0x80, 0x0f, 0xff, 0xe0, 0x0f, 0xf0, 0x03, 0xfc, 0xff, 0x00, 0x7f, 0x80, 0x1f, 0xff, 0xe0, 0x07, 
	0xf8, 0x03, 0xfc, 0xff, 0x00, 0x7f, 0x80, 0x1f, 0xff, 0xe0, 0x07, 0xf8, 0x03, 0xfc, 0xfe, 0x00, 
	0x7f, 0x80, 0x1f, 0xff, 0xf0, 0x07, 0xf8, 0x01, 0xfc, 0xfe, 0x00, 0x7f, 0x80, 0x3f, 0xff, 0xf0, 
	0x07, 0xf8, 0x01, 0xfc, 0xfe, 0x00, 0x7f, 0x80, 0x3f, 0xff, 0xf0, 0x07, 0xf8, 0x01, 0xfc, 0xfe, 
	0x00, 0x7f, 0x80, 0x3f, 0xff, 0xf0, 0x07, 0xf8, 0x01, 0xfc, 0xff, 0x00, 0x7f, 0x80, 0x1f, 0xff, 
	0xe0, 0x07, 0xf8, 0x01, 0xfc, 0xff, 0x00, 0x3f, 0x80, 0x1f, 0xff, 0xe0, 0x07, 0xf0, 0x03, 0xfc, 
	0xff, 0x00, 0x3f, 0xc0, 0x1f, 0xff, 0xc0, 0x0f, 0xf0, 0x03, 0xfc, 0xff, 0x00, 0x3f, 0xc0, 0x0f, 
	0xff, 0xc0, 0x0f, 0xf0, 0x03, 0xfc, 0xff, 0x00, 0x3f, 0xe0, 0x07, 0xff, 0x80, 0x0f, 0xf0, 0x03, 
	0xfc, 0xff, 0x80, 0x1f, 0xe0, 0x03, 0xfe, 0x00, 0x1f, 0xe0, 0x03, 0xf8, 0x7f, 0x80, 0x1f, 0xf0, 
	0x00, 0x70, 0x00, 0x1f, 0xe0, 0x07, 0xf8, 0x7f, 0x80, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x3f, 0xe0, 
	0x07, 0xf8, 0x7f, 0x80, 0x0f, 0xf8, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0x07, 0xf8, 0x3f, 0xc0, 0x0f, 
	0xfc, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0x0f, 0xf0, 0x3f, 0xe0, 0x07, 0xfc, 0x00, 0x00, 0x00, 0xff, 
	0x80, 0x0f, 0xf0, 0x3f, 0xe0, 0x07, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x00, 0x1f, 0xf0, 0x1f, 0xf0, 
	0x03, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x00, 0x1f, 0xe0, 0x1f, 0xf0, 0x01, 0xfc, 0x00, 0x00, 0x00, 
	0xfe, 0x00, 0x3f, 0xe0, 0x0f, 0xf8, 0x00, 0xfc, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x7f, 0xc0, 0x0f, 
	0xf8, 0x00, 0x70, 0x00, 0x00, 0x00, 0x38, 0x00, 0x7f, 0x80, 0x07, 0xfc, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0xff, 0x80, 0x03, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0x00, 
	0x03, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xff, 0x00, 0x01, 0xff, 0x80, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x03, 0xfe, 0x00, 0x00, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 
	0x00, 0x00, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xfc, 0x00, 0x00, 0x7f, 0xc0, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x0f, 0xf8, 0x00, 0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 
	0xf0, 0x00, 0x00, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xe0, 0x00, 0x00, 0x0f, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xc0, 0x00
 };
static const uint8_t PROGMEM connect_button_frame1[] = { 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x78, 0x00, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xfc, 0x00, 0x00, 0x00, 
	0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xfc, 0x00, 0x00, 0x00, 0xfe, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x03, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x00, 0x00, 
	0x00, 0xff, 0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x80, 0x00, 0x00, 
	0x00, 0x00, 0x0f, 0xf8, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xf8, 0x00, 
	0x00, 0x00, 0x7f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x00, 
	0x00, 0x00, 0x00, 0x1f, 0xe0, 0x00, 0x70, 0x00, 0x1f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 
	0x01, 0xfe, 0x00, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x07, 0xff, 0x80, 0x0f, 0xf0, 
	0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x0f, 0xff, 0xc0, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3f, 
	0x80, 0x0f, 0xff, 0xe0, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x1f, 0xff, 0xe0, 0x07, 
	0xf8, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x1f, 0xff, 0xe0, 0x07, 0xf8, 0x00, 0x00, 0x00, 0x00, 
	0x7f, 0x80, 0x1f, 0xff, 0xf0, 0x07, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x3f, 0xff, 0xf0, 
	0x07, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x3f, 0xff, 0xf0, 0x07, 0xf8, 0x00, 0x00, 0x00, 
	0x00, 0x7f, 0x80, 0x3f, 0xff, 0xf0, 0x07, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x80, 0x1f, 0xff, 
	0xe0, 0x07, 0xf8, 0x00, 0x00, 0x00, 0x00, 0x3f, 0x80, 0x1f, 0xff, 0xe0, 0x07, 0xf0, 0x00, 0x00, 
	0x00, 0x00, 0x3f, 0xc0, 0x1f, 0xff, 0xc0, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x0f, 
	0xff, 0xc0, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xe0, 0x07, 0xff, 0x80, 0x0f, 0xf0, 0x00, 
	0x00, 0x00, 0x00, 0x1f, 0xe0, 0x03, 0xfe, 0x00, 0x1f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xf0, 
	0x00, 0x70, 0x00, 0x1f, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xf0, 0x00, 0x00, 0x00, 0x3f, 0xe0, 
	0x00, 0x00, 0x00, 0x00, 0x0f, 0xf8, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x0f, 
	0xfc, 0x00, 0x00, 0x00, 0x7f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x00, 0x00, 0x00, 0xff, 
	0x80, 0x00, 0x00, 0x00, 0x00, 0x07, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x03, 0xfc, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xfc, 0x00, 0x00, 0x00, 
	0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
 };
static const uint8_t PROGMEM connect_button_frame0[] = { 
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x01, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0x80, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x0f, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xe0, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x1f, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xf0, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x3f, 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 
	0xe0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xe0, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x1f, 0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 
	0xff, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xff, 0x80, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xfe, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
 };

const uint8_t* connectButtonFrames[frameCount] = {
  connect_button_frame0,
  connect_button_frame1,
  connect_button_frame2
};

// Funções de som do buzzer
void playSuccessSound() {
  tone(buzzerPin, 1500, 150); // Beep agudo
  delay(200);
  tone(buzzerPin, 2000, 150); // Beep mais agudo
  delay(200);
  noTone(buzzerPin);
}

void playErrorSound() {
  tone(buzzerPin, 500, 200);  // Beep grave
  delay(250);
  tone(buzzerPin, 400, 200);  // Beep mais grave
  delay(250);
  tone(buzzerPin, 300, 300);  // Beep muito grave
  delay(350);
  noTone(buzzerPin);
}

void playWelcomeSound(String type) {
  if (type == "in") {
    // Som de entrada - tons ascendentes
    tone(buzzerPin, 800, 100);
    delay(120);
    tone(buzzerPin, 1000, 100);
    delay(120);
    tone(buzzerPin, 1200, 150);
    delay(200);
    noTone(buzzerPin);
  } else if (type == "out") {
    // Som de saída - tons descendentes
    tone(buzzerPin, 1200, 100);
    delay(120);
    tone(buzzerPin, 1000, 100);
    delay(120);
    tone(buzzerPin, 800, 150);
    delay(200);
    noTone(buzzerPin);
  }
}

// Funções auxiliares para configuração WiFi e API
void loadWiFiCredentials() {
  preferences.begin("wifi", false);
  ssid = preferences.getString("ssid", "");  
  password = preferences.getString("password", ""); 
  preferences.end();
  
  Serial.println("Loaded WiFi credentials:");
  Serial.print("SSID: "); Serial.println(ssid);
  Serial.print("Password: "); Serial.println(password.length() > 0 ? "****" : "(empty)");
}

void loadAPIConfig() {
  preferences.begin("api", false);
  apiEndpoint = preferences.getString("endpoint", "http://192.168.1.68:3000");  // Valor padrão de reserva
  preferences.end();
  
  Serial.println("Loaded API configuration:");
  Serial.print("API Endpoint: "); Serial.println(apiEndpoint);
}

void saveWiFiCredentials(String newSSID, String newPassword) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", newSSID);
  preferences.putString("password", newPassword);
  preferences.end();
  
  ssid = newSSID;
  password = newPassword;
  
  Serial.println("WiFi credentials saved!");
  Serial.print("New SSID: "); Serial.println(ssid);
}

void saveAPIConfig(String newEndpoint) {
  preferences.begin("api", false);
  preferences.putString("endpoint", newEndpoint);
  preferences.end();
  
  apiEndpoint = newEndpoint;
  
  Serial.println("API configuration saved!");
  Serial.print("New API Endpoint: "); Serial.println(apiEndpoint);
}

bool isAdminCard(String cardUID) {
  for (int i = 0; i < ADMIN_CARDS_COUNT; i++) {
    if (cardUID == ADMIN_CARDS[i]) {
      Serial.println("*** ADMIN CARD DETECTED ***");
      return true;
    }
  }
  return false;
}

void startConfigPortal() {
  Serial.println("Starting WiFi Config Portal...");
  wifiConfigMode = true;
  wifiFailedMode = false;
  
  // Parar qualquer ligação WiFi existente
  WiFi.disconnect();
  delay(100);
  
  // Iniciar Ponto de Acesso
  WiFi.mode(WIFI_AP);
  WiFi.softAP("MESAPLUS-CONFIG", "12345678");  // Nome e senha do AP
  
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  
  // Iniciar servidor DNS para portal cativo
  dnsServer.start(DNS_PORT, "*", IP);
  
  // Configurar rotas do servidor web
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.onNotFound(handleRoot);  // Redirecionar tudo para raiz para portal cativo
  
  server.begin();
  Serial.println("Web server started on http://192.168.4.1");
  
  // Mostrar ecrã de configuração
  drawConfigScreen();
  playSuccessSound();
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
  html += ".container{max-width:500px;margin:auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
  html += "h1{color:#333;text-align:center;}h2{color:#666;font-size:18px;margin-top:20px;border-bottom:2px solid #4CAF50;padding-bottom:5px;}";
  html += "label{display:block;margin-top:10px;color:#555;font-weight:bold;}";
  html += "input{width:100%;padding:12px;margin:8px 0;box-sizing:border-box;border:2px solid #ddd;border-radius:5px;}";
  html += "button{width:100%;background:#4CAF50;color:white;padding:14px;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin-top:15px;}";
  html += "button:hover{background:#45a049;}.note{font-size:12px;color:#999;margin-top:5px;}";
  html += ".logo{width:100px;height:100px;margin:0 auto 20px;display:block;}</style></head><body>";
  html += "<div class='container'>";
  html += "<svg class='logo' viewBox='0 0 100 100' xmlns='http://www.w3.org/2000/svg'>";
  html += "<defs><linearGradient id='brandGradient' x1='0%' y1='0%' x2='100%' y2='100%'>";
  html += "<stop offset='0%' stop-color='#a78bfa'/><stop offset='100%' stop-color='#f472b6'/></linearGradient>";
  html += "<filter id='simpleShadow' x='-20%' y='-20%' width='140%' height='140%'>";
  html += "<feDropShadow dx='0' dy='2' stdDeviation='4' flood-color='#000' flood-opacity='0.1'/></filter></defs>";
  html += "<circle cx='50' cy='50' r='50' fill='#0f172a' filter='url(#simpleShadow)'/>";
  html += "<circle cx='50' cy='50' r='47' fill='url(#brandGradient)' opacity='0.15'/>";
  html += "<circle cx='50' cy='50' r='47' fill='none' stroke='url(#brandGradient)' stroke-width='2' opacity='0.6'/>";
  html += "<circle cx='50' cy='50' r='45' fill='none' stroke='#ffffff' stroke-width='0.5' opacity='0.2'/>";
  html += "<g transform='translate(50,50) scale(1.3) translate(-50,-50)'>";
  html += "<path d='M30 45 Q30 35 40 35 Q45 30 50 30 Q55 30 60 35 Q70 35 70 45 Q70 50 65 50 L65 60 Q65 65 60 65 L40 65 Q35 65 35 60 L35 50 Q30 50 30 45 Z' fill='#ffffff' stroke='#e2e8f0' stroke-width='2'/>";
  html += "<ellipse cx='45' cy='42' rx='3' ry='2' fill='#ffffff' opacity='0.7'/></g></svg></svg>";
  html += "<h1>MESA+ EQUIPAMENTO - CONFIGURACAO</h1>";
  html += "<form action='/save' method='POST'>";
  
  html += "<h2>📡 WiFi</h2>";
  html += "<label>SSID do WiFi:</label><input type='text' name='ssid' placeholder='Nome da rede WiFi' value='" + ssid + "' required>";
  html += "<label>Palavra-passe WiFi:</label><input type='password' name='password' placeholder='Palavra-passe da rede' value='" + password + "' required>";
  
  html += "<h2>🌐 API</h2>";
  html += "<label>URL Base da API:</label>";
  html += "<input type='text' name='api' placeholder='http://192.168.1.89:3001' value='" + apiEndpoint + "' required>";
  html += "<div class='note'>Exemplo: http://192.168.1.89:3001 (SEM /api/ponto)</div>";
  
  html += "<button type='submit'>Guardar e Reiniciar</button>";
  html += "</form></div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("ssid") && server.hasArg("password") && server.hasArg("api")) {
    String newSSID = server.arg("ssid");
    String newPassword = server.arg("password");
    String newAPI = server.arg("api");
    
    saveWiFiCredentials(newSSID, newPassword);
    saveAPIConfig(newAPI);
    
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta charset='UTF-8'>";
    html += "<meta http-equiv='refresh' content='3;url=/'>";
    html += "<style>body{font-family:Arial;text-align:center;margin-top:50px;background:#f0f0f0;}";
    html += ".success{background:white;padding:30px;border-radius:10px;display:inline-block;box-shadow:0 2px 10px rgba(0,0,0,0.1);}";
    html += "h1{color:#4CAF50;}</style></head><body>";
    html += "<div class='success'><h1>✅ Guardado!</h1><p>A conectar ao WiFi...</p><p>O dispositivo vai reiniciar.</p></div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
    
    delay(2000);
    ESP.restart();  // Reiniciar para conectar com as novas credenciais
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void drawEnrollmentScreen(String userName, String code) {
  tft.fillScreen(0x07FF);  // Cyan background
  tft.fillRect(0, 0, 480, 51, 0xEF5D);  // Top bar
  
  // Display time
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(currentTime, 14, 9);
  
  // Display WiFi icon
  if (wifiConnected) {
    tft.drawBitmap(436, 8, image_wifi_connected_bits, 38, 32, TFT_BLACK);
  } else {
    tft.drawBitmap(436, 8, image_wifi_not_connected_bits, 38, 32, TFT_BLACK);
  }
  
  // Title
  tft.setTextSize(3);
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("MODO DE VINCULACAO", 240, 100);
  
  // User name
  tft.setTextSize(4);
  tft.setTextColor(0x001F);  // Blue
  tft.drawString(userName, 240, 140);
  
  // Code
  tft.setTextSize(2);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("Codigo: " + code, 240, 180);
  
  // Instruction
  tft.setTextSize(3);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("Aproxime o cartao NFC", 240, 230);
  tft.drawString("para vincular", 240, 260);
}

void drawEnrollmentSuccess(String userName) {
  tft.fillScreen(0x07E0);  // Green background
  tft.fillRect(0, 0, 480, 51, 0xEF5D);  // Top bar
  
  // Display time
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(currentTime, 14, 9);
  
  // Display WiFi icon
  if (wifiConnected) {
    tft.drawBitmap(436, 8, image_wifi_connected_bits, 38, 32, TFT_BLACK);
  } else {
    tft.drawBitmap(436, 8, image_wifi_not_connected_bits, 38, 32, TFT_BLACK);
  }
  
  // Success icon (checkmark)
  tft.fillCircle(240, 120, 50, TFT_WHITE);
  tft.fillCircle(240, 120, 45, 0x07E0);
  tft.setTextSize(6);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("✓", 243, 120);
  
  // Success message
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("VINCULACAO SUCESSO!", 240, 200);
  
  tft.setTextSize(4);
  tft.drawString(userName, 240, 240);
}

void drawEnrollmentError(String errorMsg) {
  tft.fillScreen(0xF800);  // Red background
  tft.fillRect(0, 0, 480, 51, 0xEF5D);  // Top bar
  
  // Display time
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(currentTime, 14, 9);
  
  // Display WiFi icon
  if (wifiConnected) {
    tft.drawBitmap(436, 8, image_wifi_connected_bits, 38, 32, TFT_BLACK);
  } else {
    tft.drawBitmap(436, 8, image_wifi_not_connected_bits, 38, 32, TFT_BLACK);
  }
  
  // Error icon
  tft.fillCircle(240, 120, 50, TFT_WHITE);
  tft.fillCircle(240, 120, 45, 0xF800);
  tft.setTextSize(6);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("X", 243, 120);
  
  // Error message
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("ERRO NA VINCULACAO", 240, 200);
  
  tft.setTextSize(2);
  tft.drawString(errorMsg, 240, 240);
}

bool checkAPIConnectivity() {
  if (apiEndpoint == "") {
    Serial.println("[API] Endpoint não configurado");
    return false;
  }
  
  HTTPClient http;
  String healthUrl = apiEndpoint + "/health";
  Serial.printf("[API] A verificar conectividade: %s\n", healthUrl.c_str());
  
  http.begin(healthUrl);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    Serial.printf("[API] Resposta: %d\n", httpResponseCode);
    if (httpResponseCode == 200) {
      Serial.println("[API] ✅ Conectividade API verificada");
      http.end();
      return true;
    } else {
      Serial.printf("[API] ❌ Código de resposta inesperado: %d\n", httpResponseCode);
    }
  } else {
    Serial.printf("[API] ❌ Erro na ligação: %s\n", http.errorToString(httpResponseCode).c_str());
  }
  
  http.end();
  return false;
}

// Heartbeat function to send life signals to API
void sendHeartbeat() {
  if (!wifiConnected || apiEndpoint == "") {
    return;
  }
  
  unsigned long currentMillis = millis();
  if (currentMillis - lastHeartbeat >= heartbeatInterval) {
    Serial.println("[HEARTBEAT] Enviando sinal de vida...");
    
    HTTPClient http;
    String heartbeatUrl = apiEndpoint + "/api/presencas/dispositivos/" + deviceId + "/heartbeat";
    
    http.begin(heartbeatUrl);
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(512);
    doc["nome"] = deviceName;
    doc["localizacao"] = deviceLocation;
    doc["firmware_version"] = firmwareVersion;
    doc["ip_address"] = WiFi.localIP().toString();
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    int httpResponseCode = http.POST(jsonString);
    
    if (httpResponseCode > 0) {
      if (httpResponseCode == 200) {
        Serial.println("[HEARTBEAT] ✅ Sinal de vida enviado com sucesso");
      } else {
        Serial.printf("[HEARTBEAT] ❌ Código de resposta: %d\n", httpResponseCode);
      }
    } else {
      Serial.printf("[HEARTBEAT] ❌ Erro na ligação: %s\n", http.errorToString(httpResponseCode).c_str());
    }
    
    http.end();
    lastHeartbeat = currentMillis;
  }
}

// Check enrollment status from API
void checkEnrollmentStatus() {
  if (!wifiConnected || apiEndpoint == "" || enrollmentMode) {
    return;
  }
  
  unsigned long currentMillis = millis();
  if (currentMillis - lastEnrollmentCheck >= enrollmentCheckInterval) {
    Serial.println("[ENROLLMENT] Verificando status de vinculação...");
    
    HTTPClient http;
    String enrollmentUrl = apiEndpoint + "/api/presencas/dispositivos/" + deviceId + "/enrollment-status";
    
    http.begin(enrollmentUrl);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      if (httpResponseCode == 200) {
        String response = http.getString();
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error) {
          bool enrollmentActive = doc["enrollmentActive"];
          if (enrollmentActive && !enrollmentMode) {
            // Start enrollment mode
            enrollmentMode = true;
            enrollmentCode = doc["codigo"].as<String>();
            enrollmentUserName = doc["userName"].as<String>();
            enrollmentStartTime = millis();
            
            Serial.println("[ENROLLMENT] Modo de vinculação iniciado");
            Serial.printf("  Código: %s\n", enrollmentCode.c_str());
            Serial.printf("  Utilizador: %s\n", enrollmentUserName.c_str());
            
            drawEnrollmentScreen(enrollmentUserName, enrollmentCode);
            playSuccessSound();
          }
        }
      }
    }
    
    http.end();
    lastEnrollmentCheck = currentMillis;
  }
}

// Send log to API
void sendLog(String level, String message) {
  if (!wifiConnected || apiEndpoint == "") {
    return;
  }
  
  HTTPClient http;
  String logUrl = apiEndpoint + "/api/presencas/dispositivos/" + deviceId + "/log";
  
  http.begin(logUrl);
  http.addHeader("Content-Type", "application/json");
  
  DynamicJsonDocument doc(512);
  doc["tipo_log"] = "device_log";
  doc["mensagem"] = message;
  doc["nivel"] = level;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  // Don't wait for response to avoid blocking
  http.POST(jsonString);
  http.end();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== MESA+ SISTEMA DE PICAR O PONTO ===");
  
  // Inicializar pino do buzzer
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);
  noTone(buzzerPin); // Ensure buzzer is off after setup
  
  // Inicializar ecrã TFT
  tft.init();
  tft.setRotation(1); // Modo paisagem 480x320
  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("A inicializar dispositivo...", 50, 150);
  
  // Carregar credenciais WiFi e configuração API guardadas
  loadWiFiCredentials();
  loadAPIConfig();
  
  // Inicializar PN532 PRIMEIRO (necessário para deteção de cartões admin)
  Wire.begin(PN532_SDA, PN532_SCL);
  nfc.begin();
  
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (versiondata) {
    Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
    Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
    Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
    nfc.SAMConfig();
    nfcInitialized = true;
    Serial.println("PN532 initialized successfully");
  } else {
    Serial.println("Didn't find PN53x board");
    nfcInitialized = false;
    playErrorSound();
  }
  
  // Conectar WiFi
  if (ssid.length() > 0) {
    Serial.print("Connecting to: "); Serial.println(ssid);
    WiFi.begin(ssid.c_str(), password.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    Serial.println();
    
    wifiConnected = (WiFi.status() == WL_CONNECTED);
    
    if (wifiConnected) {
      Serial.println("✅ WiFi connected!");
      Serial.print("IP: "); Serial.println(WiFi.localIP());
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      playSuccessSound();
      
      // Verificar conectividade da API
      if (checkAPIConnectivity()) {
        Serial.printf("API Base URL: %s\n", apiEndpoint.c_str());
        
        // Send initial heartbeat
        sendHeartbeat();
        sendLog("info", "Dispositivo iniciado e conectado");
      } else {
        Serial.println("❌ Falha na conectividade da API!");
        Serial.println("Verifique a configuração da API e rede.");
        wifiFailedMode = true;
        playErrorSound();
        drawWiFiFailedScreen();
        return; // Não continuar se API não estiver acessível
      }
      
      draw();
    } else {
      Serial.println("❌ WiFi connection FAILED!");
      Serial.println("Waiting for ADMIN card to configure WiFi...");
      wifiFailedMode = true;
      playErrorSound();
      drawWiFiFailedScreen();
    }
  } else {
    Serial.println("No WiFi credentials found!");
    wifiFailedMode = true;
    drawWiFiFailedScreen();
  }
}

void loop() {
  noTone(buzzerPin); // Force buzzer off at the start of every loop
  
  // Gerir modo de configuração WiFi
  if (wifiConfigMode) {
    dnsServer.processNextRequest();
    server.handleClient();
    return;  // Não fazer mais nada no modo de configuração
  }
  
  // Send heartbeat every 30 seconds
  if (wifiConnected) {
    sendHeartbeat();
    
    // Check enrollment status periodically
    checkEnrollmentStatus();
  }
  
  // Verificar timeout do modo de vinculação
  if (enrollmentMode && (millis() - enrollmentStartTime > enrollmentTimeout)) {
    Serial.println("[ENROLLMENT] Timeout do modo de vinculação");
    enrollmentMode = false;
    enrollmentCode = "";
    enrollmentUserName = "";
    showErrorMessage("Tempo esgotado");
    delay(2000);
    draw();
  }
  
  // Gerir timeout do ecrã de boas-vindas
  if (showWelcomeScreen && millis() - welcomeStartTime >= welcomeDisplayDuration) {
    showWelcomeScreen = false;
    welcomeUsername = "";
    welcomeType = "";  // Limpar o tipo também
    
    if (wifiConnected) {
      draw(); // Redesenhar ecrã principal
    } else if (wifiFailedMode) {
      drawWiFiFailedScreen(); // Voltar ao ecrã de erro
    }
  }
  
  // Debug: Status a cada 10 segundos
  static unsigned long lastDebugTime = 0;
  if (millis() - lastDebugTime > 10000) {
    Serial.println("\n=== STATUS ===");
    Serial.printf("WiFi: %s\n", wifiConnected ? "CONECTADO" : "DESCONECTADO");
    Serial.printf("NFC: %s\n", nfcInitialized ? "OK" : "ERRO");
    Serial.printf("Enrollment Mode: %s\n", enrollmentMode ? "SIM" : "NAO");
    Serial.printf("Welcome Screen: %s\n", showWelcomeScreen ? "SIM" : "NAO");
    Serial.println("Aguardando cartões...");
    lastDebugTime = millis();
  }
  
  // Verificar NFC (para cartões admin ou utilizadores normais)
  // Permitir leitura sempre que não estiver mostrando boas-vindas
  if (nfcInitialized && !showWelcomeScreen) {
    if (wifiFailedMode) {
      // Se WiFi falhou, apenas verificar cartões admin
      checkForAdminCard();
    } else if (wifiConnected) {
      // Se WiFi ligado, verificar cartões (admin abre config, normal faz registo, ou vinculação)
      checkForNFC();
    }
  }
  
  // Atualizar hora a cada segundo (apenas se WiFi ligado)
  if (wifiConnected && millis() - lastTimeUpdate >= timeUpdateInterval) {
    updateTime();
    lastTimeUpdate = millis();
    
    // Atualizar apenas a área de exibição da hora, não o ecrã inteiro
    if (!showWelcomeScreen && currentTime != previousTime) {
      updateTimeDisplay();
      previousTime = currentTime;
    }
  }
  
  // Verificar estado do WiFi (apenas se pensarmos que estamos ligados e não em modo de configuração)
  if (wifiConnected && !wifiConfigMode) {
    bool currentWifiStatus = (WiFi.status() == WL_CONNECTED);
    if (currentWifiStatus != wifiConnected) {
      wifiConnected = currentWifiStatus;
      wifiStatusChanged = true;
      
      // Reproduzir som baseado na mudança de estado do WiFi
      if (wifiConnected) {
        playSuccessSound(); // WiFi reconectado
      } else {
        playErrorSound(); // WiFi desconectado
        wifiFailedMode = true;
        drawWiFiFailedScreen();
      }
      
      // Atualizar apenas a área do ícone WiFi
      if (!showWelcomeScreen && !wifiFailedMode) {
        updateWiFiDisplay();
      }
    }
  }
  
  // Atualizar frame de animação com efeito de salto (apenas se ligado e não mostrando outros ecrãs)
  if (wifiConnected && !showWelcomeScreen && !wifiFailedMode && !wifiConfigMode && millis() - lastFrameTime >= frameDuration) {
    updateAnimationFrame();
    lastFrameTime = millis();
    drawConnectButtonFrame(frameIndex);
  }
  
  // Redesenhar ecrã inteiro apenas se mostrando boas-vindas e estado mudou
  if (!showWelcomeScreen && wifiStatusChanged) {
    wifiStatusChanged = false;
  }
  
  delay(50);
}

void updateAnimationFrame() {
  if (animationDirection) {
    // Avançar
    frameIndex++;
    if (frameIndex >= frameCount - 1) {
      frameIndex = frameCount - 1;
      animationDirection = false; // Começar a retroceder
    }
  } else {
    // Retroceder
    frameIndex--;
    if (frameIndex <= 0) {
      frameIndex = 0;
      animationDirection = true; // Começar a avançar
    }
  }
}

void updateTimeDisplay() {
  // Limpar apenas a área da hora e redesenhar
  tft.fillRect(0, 0, 200, 51, 0xEF5D);  // Limpar área da hora
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(currentTime, 14, 9);
}

void updateWiFiDisplay() {
  // Limpar apenas a área do ícone WiFi e redesenhar
  tft.fillRect(436, 8, 38, 32, 0xEF5D);  // Limpar área WiFi
  if (wifiConnected) {
    tft.drawBitmap(436, 8, image_wifi_connected_bits, 38, 32, TFT_BLACK);
  } else {
    tft.drawBitmap(436, 8, image_wifi_not_connected_bits, 38, 32, TFT_BLACK);
  }
  
  // Atualizar texto de estado na parte inferior
  tft.fillRect(10, 280, 200, 10, 0xBDF7); // Limpar área de estado
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK);
  if (!wifiConnected) {
    tft.drawString("WiFi Disconnected", 10, 280);
  }
}

void checkForAdminCard() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
  uint8_t uidLength;
  
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);
  
  if (success) {
    String cardUID = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) cardUID += "0";
      cardUID += String(uid[i], HEX);
    }
    cardUID.toUpperCase();
    
    Serial.println("\n🔍 Cartão detetado (modo WiFi falhou)!");
    Serial.print("📇 UID do cartão: "); Serial.println(cardUID);
    
    if (isAdminCard(cardUID)) {
      startConfigPortal();
    } else {
      Serial.println("⛔ Não é cartão admin - a ignorar");
      tone(buzzerPin, 300, 200);  // Low beep for non-admin
      delay(250);
      noTone(buzzerPin);
    }
    
    // Aguardar remoção do cartão
    while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
      delay(100);
    }
  }
}

void checkForNFC() {
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer para armazenar o UID retornado
  uint8_t uidLength;                        // Comprimento do UID (4 ou 7 bytes dependendo do tipo de cartão ISO14443A)
  
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100);
  
  if (success) {
    // Converter UID para string hexadecimal
    String cardUID = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if (uid[i] < 0x10) cardUID += "0";
      cardUID += String(uid[i], HEX);
    }
    cardUID.toUpperCase();
    
    Serial.println("\n📇 Cartão NFC detetado!");
    Serial.print("UID: "); Serial.println(cardUID);
    
    // Verificar se é cartão de administrador
    if (isAdminCard(cardUID)) {
      Serial.println("*** Cartão ADMIN detetado - a iniciar modo de configuração ***");
      startConfigPortal();
      // Aguardar remoção do cartão
      while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
        delay(100);
      }
      return;
    }
    
    // Verificar se está em modo de vinculação
    if (enrollmentMode) {
      Serial.println("📋 Modo de vinculação - a vincular cartão...");
      playSuccessSound();
      
      // Enviar pedido de vinculação (apiEndpoint é base URL)
      String vincularEndpoint = apiEndpoint + "/api/presencas/vincular";
      
      HTTPClient http;
      http.begin(vincularEndpoint);
      http.addHeader("Content-Type", "application/json");
      
      Serial.print("Sending enrollment to: "); Serial.println(vincularEndpoint);
      
      DynamicJsonDocument doc(512);
      doc["uid"] = cardUID;
      doc["codigo"] = enrollmentCode;
      doc["device_id"] = deviceId;
      
      String jsonString;
      serializeJson(doc, jsonString);
      
      Serial.print("Enviando vinculação: "); Serial.println(jsonString);
      
      int httpResponseCode = http.POST(jsonString);
      
      if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.print("Resposta: "); Serial.println(response);
        
        DynamicJsonDocument responseDoc(1024);
        deserializeJson(responseDoc, response);
        
        if (responseDoc.containsKey("error")) {
          String error = responseDoc["error"];
          drawEnrollmentError(error);
          sendLog("erro", "Erro na vinculação: " + error);
          delay(3000);
          enrollmentMode = false;
          enrollmentCode = "";
          enrollmentUserName = "";
          draw();
        } else {
          // Enrollment success
          String userName = responseDoc["name"].as<String>();
          drawEnrollmentSuccess(userName);
          sendLog("info", "Cartão vinculado com sucesso: " + cardUID);
          
          delay(3000);
          enrollmentMode = false;
          enrollmentCode = "";
          enrollmentUserName = "";
          draw();
        }
      } else {
        drawEnrollmentError("Erro de conexao");
        sendLog("erro", "Erro HTTP na vinculação: " + String(httpResponseCode));
        delay(3000);
        enrollmentMode = false;
        enrollmentCode = "";
        enrollmentUserName = "";
        draw();
      }
      
      http.end();
      
      // Aguardar remoção do cartão
      while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
        delay(100);
      }
      return;
    }
    
    // Modo normal - reproduzir som de deteção de cartão
    playSuccessSound();
    
    // Enviar pedido API
    sendUserRequest(cardUID);
    sendLog("info", "Cartão lido: " + cardUID);
    
    // Aguardar remoção do cartão para evitar múltiplas leituras
    while (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
      delay(100);
    }
  }
}

void sendUserRequest(String cardUID) {
  if (!wifiConnected) {
    Serial.println("WiFi not connected, cannot send API request");
    playErrorSound(); // Play error sound for no WiFi
    showErrorMessage("WiFi desconectado");
    return;
  }
  
  // Construir endpoint completo (apiEndpoint deve ser base URL, ex: http://192.168.1.89:3001)
  String pontoEndpoint = apiEndpoint + "/api/ponto";
  
  HTTPClient http;
  http.begin(pontoEndpoint);
  http.addHeader("Content-Type", "application/json");
  
  // Criar payload JSON
  DynamicJsonDocument doc(1024);
  doc["uid"] = cardUID;
  doc["device_id"] = deviceId;
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.print("Sending POST request to: "); Serial.println(pontoEndpoint);
  Serial.print("Payload: "); Serial.println(jsonString);
  
  int httpResponseCode = http.POST(jsonString);
  
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.print("HTTP Response Code: "); Serial.println(httpResponseCode);
    Serial.print("Response Length: "); Serial.println(response.length());
    Serial.print("Response (first 200 chars): "); Serial.println(response.substring(0, 200));
    
    // Verificar se a resposta é HTML (erro comum)
    if (response.startsWith("<!DOCTYPE") || response.startsWith("<html")) {
      Serial.println("⚠️ ERRO: Recebeu HTML em vez de JSON!");
      Serial.println("Verifique se o endpoint está correto.");
      Serial.printf("Base URL configurada: %s\n", apiEndpoint.c_str());
      Serial.printf("Endpoint completo: %s\n", pontoEndpoint.c_str());
      playErrorSound();
      showErrorMessage("Endpoint incorreto");
      http.end();
      return;
    }
    
    // Analisar resposta JSON
    DynamicJsonDocument responseDoc(1024);
    DeserializationError error = deserializeJson(responseDoc, response);
    
    if (error) {
      Serial.print("⚠️ Erro ao analisar JSON: ");
      Serial.println(error.c_str());
      playErrorSound();
      showErrorMessage("Erro JSON");
      http.end();
      return;
    }
    
    if (responseDoc.containsKey("error")) {
      String errorMsg = responseDoc["error"];
      Serial.print("API Error: "); Serial.println(errorMsg);
      playErrorSound(); // Play error sound for API error
      showErrorMessage(errorMsg);
    } else if (responseDoc.containsKey("username") && responseDoc.containsKey("type")) {
      String username = responseDoc["username"];
      String type = responseDoc["type"];  // "in" ou "out"
      Serial.print("✅ User: "); Serial.print(username);
      Serial.print(" - Type: "); Serial.println(type);
      
      // Reproduzir som de boas-vindas baseado no tipo
      playWelcomeSound(type);
      
      showWelcomeMessage(username, type);  // Passar ambos os parâmetros
    } else {
      Serial.println("⚠️ Unknown response format");
      Serial.println("Response keys:");
      JsonObject obj = responseDoc.as<JsonObject>();
      for (JsonPair kv : obj) {
        Serial.printf("  - %s\n", kv.key().c_str());
      }
      playErrorSound(); // Play error sound for unknown response
      showErrorMessage("Resposta invalida");
    }
  } else {
    Serial.print("HTTP Error: "); Serial.println(httpResponseCode);
    playErrorSound(); // Play error sound for HTTP error
    showErrorMessage("Erro de ligacao");
  }
  
  http.end();
}

void showWelcomeMessage(String username, String type) {
  showWelcomeScreen = true;
  welcomeUsername = username;
  welcomeType = type;  // Guardar o tipo
  welcomeStartTime = millis();
  drawWelcomeScreen();
}

void showErrorMessage(String error) {
  // Mostrar erro temporariamente
  tft.fillScreen(0xBDF7);
  tft.fillRect(0, 0, 480, 51, 0xEF5D);  // Barra superior
  
  // Display time
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(currentTime, 14, 9);
  
  // Display WiFi icon
  if (wifiConnected) {
    tft.drawBitmap(436, 8, image_wifi_connected_bits, 38, 32, TFT_BLACK);
  } else {
    tft.drawBitmap(436, 8, image_wifi_not_connected_bits, 38, 32, TFT_BLACK);
  }
  
  // Show error message
  tft.setTextSize(3);
  tft.setTextColor(TFT_RED);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("ERRO:", 240, 130);
  tft.setTextSize(2);
  tft.drawString(error, 240, 170);
  
  delay(3000); // Show error for 3 seconds
  draw(); // Return to main screen
}

void drawWiFiFailedScreen() {
  tft.fillScreen(0xF800);  // Fundo vermelho para erro
  
  // Área do ícone de erro
  tft.fillCircle(240, 100, 50, TFT_WHITE);
  tft.fillCircle(240, 100, 45, 0xF800);
  tft.setTextSize(6);
  tft.setTextColor(TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("!", 243, 100);
  
  // Mensagem de erro
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Erro ao conectar", 240, 180);
  tft.drawString("ao WiFi", 240, 210);
  
  // Instrução
  tft.setTextSize(2);
  tft.drawString("Aproxime um cartao de administrador", 240, 260);
  tft.drawString("para configurar", 240, 285);
}

void drawConfigScreen() {
  tft.fillScreen(0x07E0);  // Fundo verde
  
  // Ícone de configuração (símbolo de engrenagem)
  tft.fillCircle(240, 80, 40, TFT_WHITE);
  tft.setTextSize(6);
  tft.setTextColor(0x07E0);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("*", 244, 81);
  
  // Título
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Modo Configuracao", 240, 150);
  
  // Instruções
  tft.setTextSize(2);
  tft.drawString("1. Ligue-se ao WiFi:", 240, 200);
  tft.setTextSize(3);
  tft.setTextColor(TFT_BLACK);
  tft.drawString("MESAPLUS-CONFIG", 240, 225);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("Senha: 12345678", 240, 255);
  tft.drawString("2. Abra o seu navegador", 240, 285);
}

void drawWelcomeScreen() {
  tft.fillScreen(0xBDF7);             // Fundo
  tft.fillRect(0, 0, 480, 51, 0xEF5D);  // Barra superior
  
  // Exibir hora
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(currentTime, 14, 9);
  
  // Exibir ícone WiFi
  if (wifiConnected) {
    tft.drawBitmap(436, 8, image_wifi_connected_bits, 38, 32, TFT_BLACK);
  } else {
    tft.drawBitmap(436, 8, image_wifi_not_connected_bits, 38, 32, TFT_BLACK);
  }
  
  // Mensagem de boas-vindas baseada no tipo
  tft.setTextSize(3);
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  
  if (welcomeType == "in") {
    tft.drawString("Bem vindo/a,", 240, 140);
  } else if (welcomeType == "out") {
    tft.drawString("Tenha um bom dia,", 240, 140);
  } else {
    // Mensagem alternativa se o tipo for desconhecido
    tft.drawString("Ola,", 240, 140);
  }
  
  // Exibir nome de utilizador
  tft.setTextSize(4);
  tft.setTextColor(0x07E0); // Cor verde
  tft.drawString(welcomeUsername, 240, 180);
}

void updateTime() {
  if (wifiConnected) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeString[6];
      strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo);
      currentTime = String(timeString);
    }
  }
}

void draw() {
  tft.fillScreen(0xBDF7);             // Fundo
  tft.fillRect(0, 51, 480, 270, 0xCE79); // Secção central
  tft.fillRoundRect(177, 110, 125, 125, 20, 0xEF5D); // Base do botão
  tft.fillRect(0, 0, 480, 51, 0xEF5D);  // Barra superior

  // Exibir hora
  tft.setTextColor(TFT_BLACK);
  tft.setTextSize(4);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(currentTime, 14, 9);

  // Exibir ícone WiFi
  if (wifiConnected) {
    tft.drawBitmap(436, 8, image_wifi_connected_bits, 38, 32, TFT_BLACK);
  } else {
    tft.drawBitmap(436, 8, image_wifi_not_connected_bits, 38, 32, TFT_BLACK);
  }

  // Desenhar botão de ligação animado
  drawConnectButtonFrame(frameIndex);

  // Texto de estado WiFi opcional
  tft.setTextSize(1);
  tft.setTextColor(TFT_BLACK);
  if (!wifiConnected) tft.drawString("WiFi Disconnected", 10, 280);
  
  // Estado do NFC
  if (nfcInitialized) {
  } else {
    tft.drawString("Erro no Módulo NFC", 10, 290);
  }
}

void drawConnectButtonFrame(int idx) {
  // Limpar área do botão
  tft.fillRoundRect(177, 110, 125, 125, 20, 0xEF5D);
  // Desenhar bitmap do frame centrado dentro do botão
  tft.drawBitmap(196, 144, connectButtonFrames[idx], 86, 56, TFT_BLACK);
}