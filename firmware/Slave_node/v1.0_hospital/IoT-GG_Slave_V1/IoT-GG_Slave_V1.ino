/*
 * ============================================================
 *  IoT-GG  — NŒUD ESCLAVE — FIRMWARE COMPLET v3.0
 *  Portail Captif + Interface Web Pro + Planning Garde + RTC Sync
 *  Ceci est le code du dispositif qui s'installera dans la salle de garde
 *
 *  Auteur : ESSOGO NGAMO JERRY ORLAN
 *  CFPE Douala · DTS Électronique Industrielle 2025-2026
 * ============================================================
 *
 *  BROCHAGE COMPLET :
 *    TFT CS=15 DC=4 RST=2 MOSI=23 SCK=18 MISO=19 LED=3.3V
 *    RTC SDA=21 SCL=22
 *    BUZZER GPIO25 → R1kΩ → Base NPN 2N2222
 *    BOUTON GPIO13 → GND (pull-up interne)
 *    MODULE SD CS=GPIO5 (SPI partagé avec TFT)
 *    LED RGB Anode commune = 3.3V Red + 3.3kΩ = GPIO26 Green + 3.3kΩ = GPIO27 Blue +  3.3kΩ = GPIO14
 *
 *  BIBLIOTHÈQUES :
 *    Adafruit GFX + ILI9341 · RTClib · SD · WebServer · DNSServer
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "RTClib.h"
#include <WebServer.h>
#include <DNSServer.h>   // ← Pour le portail captif

// ─────────────────────────────────────────────────────────────
//  ADRESSE MAC DU MAÎTRE
// ─────────────────────────────────────────────────────────────
// ⚠️ REMPLACEZ PAR L'ADRESSE MAC DE VOTRE NŒUD MAÎTRE
uint8_t adresseMacMaitre[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

// ─────────────────────────────────────────────────────────────
//  BROCHES
// ─────────────────────────────────────────────────────────────
#define TFT_CS     15
#define TFT_DC      4
#define TFT_RST     2
#define SD_CS       5
#define PIN_BUZZER 25
#define PIN_BOUTON 13
#define LED_R      26
#define LED_G      27
#define LED_B      14

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231       rtc;
bool             rtcDisponible = false;

// ─────────────────────────────────────────────────────────────
//  PORTAIL CAPTIF — Configuration
// ─────────────────────────────────────────────────────────────
// ⚠️ REMPLACEZ PAR VOS PROPRES IDENTIFIANTS
const char*  ssidAP     = "VOTRE_SSID_ICI";
const char*  passwordAP = "VOTRE_MOT_DE_PASSE_ICI";
const byte   DNS_PORT   = 53;
IPAddress    apIP(192, 168, 4, 1);

WebServer  server(80);
DNSServer  dnsServer;   // Redirige tout le DNS vers nous → portail captif

// ─────────────────────────────────────────────────────────────
//  COULEURS TFT (RGB565)
// ─────────────────────────────────────────────────────────────
#define CLR_BG       0x0841
#define CLR_HEADER   0x0020
#define CLR_CYAN     0x07FF
#define CLR_WHITE    0xFFFF
#define CLR_GREEN    0x07E0
#define CLR_ORANGE   0xFD20
#define CLR_RED      0xF800
#define CLR_GRAY     0x4208
#define CLR_DARKGRAY 0x2104
#define CLR_YELLOW   0xFFE0

// ─────────────────────────────────────────────────────────────
//  SEUILS & POSITIONS
// ─────────────────────────────────────────────────────────────
#define SEUIL_NORMAL    1.5f
#define SEUIL_ATTENTION 1.0f
#define SCREEN_W 320
#define SCREEN_H 240
#define BOUTEILLE_X 232
#define BOUTEILLE_Y  30
#define BOUTEILLE_W  68
#define BOUTEILLE_H 170
#define COL_X  10
#define COL_W 212

// ─────────────────────────────────────────────────────────────
//  STRUCTURES ESP-NOW
// ─────────────────────────────────────────────────────────────
typedef struct { float pression; char statut[16]; uint32_t compteur; } msg_pression;
typedef struct { bool estPing; uint32_t timestamp; }                    msg_ping;

msg_pression  dataRecue;
msg_ping      dataPing, dataPongRecu;
esp_now_peer_info_t peerInfo;

//  Timer LED
unsigned long t_led = 0;
bool led_visible    = true;

// ─────────────────────────────────────────────────────────────
//  VARIABLES GLOBALES
// ─────────────────────────────────────────────────────────────
float   pression_actuelle   = 0.0f;
float   pression_prev       = -99.0f;
String  statut_actuel       = "NORMAL";
String  statut_prev         = "";
bool    premiereReception   = false;

// Watchdog
unsigned long dernierPingEnvoye  = 0;
unsigned long dernierPongRecu    = 0;
unsigned long debutDeconnexion   = 0;
bool          estDeconnecte      = false;
const unsigned long INTERVALLE_PING      = 5000;
const unsigned long TIMEOUT_DECONNEXION  = 12000;

// Buzzer
bool          buzzerActif    = false;
bool          buzzerEtat     = false;
unsigned long t_buzz         = 0;
bool          buzzerAcquitte = false;
unsigned long t_acquitt      = 0;
const unsigned long DELAI_REDECL    = 30000;
const unsigned long ATT_ON          = 300;
const unsigned long ATT_OFF         = 1500;
const unsigned long ALE_ON          = 120;
const unsigned long ALE_OFF         = 80;

// Bouton
bool          dernierBouton  = HIGH;
unsigned long t_bouton       = 0;
const unsigned long ANTI_REBOND = 200;

// TFT
bool          statut_visible = true;
unsigned long t_blink        = 0;

// ─────────────────────────────────────────────────────────────
//  PLANNING DE GARDE (stocké en mémoire + SD)
//  Structure : nom, numéro, poste
// ─────────────────────────────────────────────────────────────
#define MAX_CONTACTS 10
struct Contact {
  char nom[32];
  char telephone[16];
  char poste[32];
  bool actif;
};

Contact planning[MAX_CONTACTS];
int nbContacts = 0;

// ─────────────────────────────────────────────────────────────
//  HISTORIQUE ALERTES (20 dernières)
// ─────────────────────────────────────────────────────────────
#define MAX_ALERTES 20
struct Alerte {
  char datetime[20];
  float pression;
  char statut[16];
};
Alerte historiqueAlertes[MAX_ALERTES];
int nbAlertes = 0;

// =============================================================
//  UTILITAIRES RTC
// =============================================================
DateTime obtenirHeure() {
  return rtcDisponible ? rtc.now() : DateTime(2026, 6, 23, 0, 0, 0);
}

void logSerie() {
  DateTime n = obtenirHeure();
  Serial.printf("[%04d-%02d-%02d %02d:%02d:%02d] ",
    n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second());
}

/*
 * Synchronise le RTC depuis une chaîne ISO : "YYYY-MM-DDTHH:MM:SS"
 * Appelée depuis la page web ou depuis le moniteur série
 */
void syncRTC(String s) {
  if (s.length() < 19) { Serial.println("Format invalide"); return; }
  int y  = s.substring(0,4).toInt(),  mo = s.substring(5,7).toInt();
  int d  = s.substring(8,10).toInt(), h  = s.substring(11,13).toInt();
  int mn = s.substring(14,16).toInt(),sc = s.substring(17,19).toInt();
  if (rtcDisponible) {
    rtc.adjust(DateTime(y, mo, d, h, mn, sc));
    logSerie();
    Serial.printf("✓ RTC sync → %04d-%02d-%02d %02d:%02d:%02d\n",y,mo,d,h,mn,sc);
  }
}

// =============================================================
//  UTILITAIRES PLANNING
// =============================================================
void sauvegarderPlanning() {
  if (!SD.begin(SD_CS)) return;
  SD.remove("/planning.csv");
  File f = SD.open("/planning.csv", FILE_WRITE);
  if (!f) return;
  f.println("Nom;Telephone;Poste");
  for (int i = 0; i < nbContacts; i++) {
    if (planning[i].actif)
      f.printf("%s;%s;%s\n", planning[i].nom, planning[i].telephone, planning[i].poste);
  }
  f.close();
}

void chargerPlanning() {
  if (!SD.begin(SD_CS)) return;
  if (!SD.exists("/planning.csv")) return;
  File f = SD.open("/planning.csv");
  if (!f) return;
  f.readStringUntil('\n'); // skip header
  nbContacts = 0;
  while (f.available() && nbContacts < MAX_CONTACTS) {
    String ligne = f.readStringUntil('\n');
    ligne.trim();
    if (ligne.length() == 0) continue;
    int s1 = ligne.indexOf(';'), s2 = ligne.indexOf(';', s1+1);
    if (s1 < 0 || s2 < 0) continue;
    strncpy(planning[nbContacts].nom,       ligne.substring(0, s1).c_str(),  31);
    strncpy(planning[nbContacts].telephone, ligne.substring(s1+1, s2).c_str(), 15);
    strncpy(planning[nbContacts].poste,     ligne.substring(s2+1).c_str(),   31);
    planning[nbContacts].actif = true;
    nbContacts++;
  }
  f.close();
  logSerie();
  Serial.printf("✓ Planning charge : %d contact(s)\n", nbContacts);
}

// Ajoute une alerte dans l'historique circulaire
void enregistrerAlerte(float p, const char* st) {
  DateTime n = obtenirHeure();
  int idx = nbAlertes % MAX_ALERTES;
  snprintf(historiqueAlertes[idx].datetime, 20, "%04d-%02d-%02d %02d:%02d",
    n.year(), n.month(), n.day(), n.hour(), n.minute());
  historiqueAlertes[idx].pression = p;
  strncpy(historiqueAlertes[idx].statut, st, 15);
  nbAlertes++;

  // Log aussi sur SD
  if (SD.begin(SD_CS)) {
    File f = SD.open("/alertes.csv", FILE_APPEND);
    if (f) {
      f.printf("%04d-%02d-%02d %02d:%02d:%02d;%.2f;%s\n",
        n.year(), n.month(), n.day(), n.hour(), n.minute(), n.second(), p, st);
      f.close();
    }
  }
}

// =============================================================
//  BUZZER
// =============================================================
uint16_t couleurStatut(float p) {
  if (p >= SEUIL_NORMAL)    return CLR_GREEN;
  if (p >= SEUIL_ATTENTION) return CLR_ORANGE;
  return CLR_RED;
}
String libelleStatut(float p) {
  if (p >= SEUIL_NORMAL)    return "NORMAL";
  if (p >= SEUIL_ATTENTION) return "ATTENTION";
  return "ALERTE CRITIQUE";
}

void demarrerBuzzer() {
  buzzerActif = true; buzzerAcquitte = false;
  buzzerEtat = true;  t_buzz = millis();
  digitalWrite(PIN_BUZZER, HIGH);
  logSerie();
  Serial.println(statut_actuel == "ALERTE CRITIQUE" ?
    ">>> BUZZER ALERTE (rapide)" : ">>> BUZZER ATTENTION (lent)");
}

void arreterBuzzer() {
  buzzerActif = false;
  digitalWrite(PIN_BUZZER, LOW);
}

void gererBuzzer() {
  if (!buzzerActif) { digitalWrite(PIN_BUZZER, LOW); return; }
  unsigned long on  = (statut_actuel == "ALERTE CRITIQUE") ? ALE_ON  : ATT_ON;
  unsigned long off = (statut_actuel == "ALERTE CRITIQUE") ? ALE_OFF : ATT_OFF;
  unsigned long now = millis();
  if (buzzerEtat  && now - t_buzz >= on)  { buzzerEtat = false; t_buzz = now; digitalWrite(PIN_BUZZER, LOW);  }
  if (!buzzerEtat && now - t_buzz >= off) { buzzerEtat = true;  t_buzz = now; digitalWrite(PIN_BUZZER, HIGH); }
}

void gererBouton() {
  bool e = digitalRead(PIN_BOUTON);
  if (e == LOW && dernierBouton == HIGH && millis() - t_bouton > ANTI_REBOND) {
    t_bouton = millis();
    if (buzzerActif) {
      arreterBuzzer(); buzzerAcquitte = true; t_acquitt = millis();
      logSerie(); Serial.println(">>> Buzzer acquitté (bouton physique)");
    }
  }
  dernierBouton = e;
}

void verifierRedeclenchement() {
  if (!buzzerAcquitte) return;
  if (millis() - t_acquitt >= DELAI_REDECL) {
    bool anormal = (statut_actuel == "ATTENTION" || statut_actuel == "ALERTE CRITIQUE");
    if (anormal) { logSerie(); Serial.println(">>> RE-DÉCLENCHEMENT buzzer"); demarrerBuzzer(); }
    else          { buzzerAcquitte = false; }
  }
}

//==========================================================
//    LED RGB (Fonctions)
//==========================================================
void initLED(){
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  eteindreRGB();
}


void eteindreRGB(){
  // Anode commune -> HIGH = éteint
  digitalWrite(LED_R, HIGH);
  digitalWrite(LED_G, HIGH);
  digitalWrite(LED_B, HIGH);
}

void setRGB(bool r, bool g, bool b){
  digitalWrite(LED_R, r ? LOW : HIGH);
  digitalWrite(LED_G, g ? LOW : HIGH);
  digitalWrite(LED_B, b ? LOW : HIGH);
}

void gererLED(float p){
  unsigned long now = millis();

  if (p >= SEUIL_NORMAL){
    // Normal = Vert fixe
    setRGB(false, true, false);
  }else if(p >= SEUIL_ATTENTION){
    // ATTENTION -> Orange clignotant lent (700ms)
    if(now - t_led >= 700){
      t_led = now;
      led_visible = !led_visible;
    }
    if(led_visible) setRGB(true, true, false); // Je suppose que le rouge + vert donnent Orange
    else            eteindreRGB();
  } else {
    // Alerte -> Rouge clignotant rapide (200 ms)
    if(now - t_led >= 200){
      t_led = now;
      led_visible = !led_visible;
    }
    if(led_visible) setRGB(true, false, false);
    else            eteindreRGB();
  }
}
// =============================================================
//  TFT — Fonctions d'affichage (layout corrigé)
// =============================================================
void dessinerTitre() {
  tft.fillRect(0, 0, SCREEN_W, 28, CLR_HEADER);
  // ── Logo IoT-GG ──
  tft.setTextSize(2);
  tft.setTextColor(CLR_CYAN);   tft.setCursor(8, 6);  tft.print("IoT-");
  tft.setTextColor(CLR_WHITE);                         tft.print("GG v1.0");
  // ── Heure au centre ── (zone dédiée, plus de chevauchement)
  // L'heure sera dessinée par mettreAJourHeure()
  // ── Filet ──
  tft.drawFastHLine(0, 28, SCREEN_W, CLR_CYAN);
}

void dessinerSeparateur() {
  for (int i = 32; i < SCREEN_H - 22; i += 3)
    tft.drawPixel(BOUTEILLE_X - 6, i, 0x2104);
}

void dessinerLabels() {
  tft.setTextSize(1);
  tft.setTextColor(CLR_CYAN);
  tft.setCursor(COL_X, 36); tft.print("PRESSION ACTUELLE");
  tft.drawFastHLine(COL_X, 47, 145, CLR_CYAN);

  // Infos bas — séparateur
  tft.drawFastHLine(COL_X, 160, COL_W, 0x4208);
  tft.setTextColor(0x8410);
  tft.setCursor(COL_X, 164); tft.print("Seuil alerte :");
  tft.setCursor(COL_X, 177); tft.print("Seuil attention :");
  tft.setCursor(COL_X, 190); tft.print("Liaison :");
  tft.setTextColor(CLR_RED);    tft.setCursor(112, 164); tft.print("< 1.00 bar");
  tft.setTextColor(CLR_ORANGE); tft.setCursor(112, 177); tft.print("< 1.50 bar");
}

void dessinerBouteille() {
  int x = BOUTEILLE_X, y = BOUTEILLE_Y, w = BOUTEILLE_W, h = BOUTEILLE_H;
  int cw = 22, ch = 16, cx = x + (w-cw)/2;
  tft.fillRoundRect(cx, y, cw, ch, 3, CLR_DARKGRAY);
  tft.drawRoundRect(cx, y, cw, ch, 3, CLR_GRAY);
  tft.fillRect(cx, y+ch-3, cw, 5, CLR_DARKGRAY);
  int cy = y+ch+1, cH = h-ch-1;
  tft.fillRoundRect(x, cy, w, cH, 7, CLR_DARKGRAY);
  tft.drawRoundRect(x, cy, w, cH, 7, CLR_GRAY);
  // Graduations
  for (int gx = x+5; gx < x+w-5; gx += 4) {
    tft.drawPixel(gx, cy + cH/4,   0x4208);
    tft.drawPixel(gx, cy + cH/2,   0x4208);
    tft.drawPixel(gx, cy+3*cH/4,   0x4208);
  }
}

void majBouteille(float p) {
  int x = BOUTEILLE_X, y = BOUTEILLE_Y, w = BOUTEILLE_W, h = BOUTEILLE_H;
  int cy = y + 17, cH = h - 17;
  float r = constrain(p / 2.5f, 0.0f, 1.0f);
  int fh = (int)(r * (cH - 14));
  int fy = cy + (cH - 14) - fh + 6;
  uint16_t fc = (r > 0.5f) ? CLR_GREEN : (r > 0.25f) ? CLR_ORANGE : CLR_RED;

  tft.fillRoundRect(x+3, cy+3, w-6, cH-6, 5, CLR_DARKGRAY);
  if (fh > 2) {
    tft.fillRoundRect(x+4, fy, w-8, fh, 4, fc);
    tft.drawFastHLine(x+8, fy+2, w-16, 0xCE79);
  }
  // Pourcentage dans la bouteille
  tft.setTextSize(2); tft.setTextColor(CLR_WHITE);
  tft.setCursor(x + w/2 - 10, cy + cH - 18);
  tft.print((int)(r*100)); tft.print("%");
}

void majPression(float p) {
  tft.fillRect(COL_X, 50, COL_W - 5, 55, CLR_BG);
  char buf[8]; dtostrf(p, 4, 2, buf);
  tft.setTextSize(4); tft.setTextColor(CLR_YELLOW);
  tft.setCursor(COL_X, 52); tft.print(buf);
  tft.setTextSize(2); tft.setTextColor(0xC618);
  tft.setCursor(COL_X + 105, 62); tft.print("bars");
  tft.setTextSize(1); tft.setTextColor(0x6B4D);
  tft.setCursor(COL_X, 100); tft.print("post-detendeur | MPX4250AP");
}

void majStatut(float p) {
  uint16_t c = couleurStatut(p);
  String   l = libelleStatut(p);
  bool clign = (p < SEUIL_ATTENTION);
  if (clign && millis() - t_blink > 400) { statut_visible = !statut_visible; t_blink = millis(); }
  else if (!clign) statut_visible = true;

  int sx=COL_X, sy=112, sw=COL_W-14, sh=36;
  tft.fillRoundRect(sx, sy, sw, sh, 8, CLR_BG);
  if (statut_visible) {
    tft.fillRoundRect(sx, sy, sw, sh, 8, c);
    tft.drawRoundRect(sx, sy, sw, sh, 8, CLR_WHITE);
    if      (p >= SEUIL_NORMAL)    { tft.fillCircle(sx+16, sy+sh/2, 5, CLR_WHITE); }
    else if (p >= SEUIL_ATTENTION) { tft.fillTriangle(sx+16,sy+7,sx+8,sy+sh-8,sx+24,sy+sh-8,CLR_WHITE); }
    else { tft.drawLine(sx+10,sy+8,sx+22,sy+sh-8,CLR_WHITE); tft.drawLine(sx+22,sy+8,sx+10,sy+sh-8,CLR_WHITE); }
    tft.setTextColor(CLR_WHITE); tft.setTextSize(2);
    int16_t tx,ty; uint16_t tw,th;
    tft.getTextBounds(l,0,0,&tx,&ty,&tw,&th);
    tft.setCursor(sx+(sw-tw)/2+14, sy+(sh-th)/2+1); tft.print(l);
  }
}

/*
 * Heure + date dans la zone dédiée du bandeau titre
 * Plus de chevauchement avec l'auteur/école
 */
void majHeure() {
  DateTime n = obtenirHeure();
  // ── Heure centrée dans le bandeau ──
  tft.fillRect(140, 3, 200, 22, CLR_HEADER);
  tft.setTextSize(2); tft.setTextColor(CLR_WHITE);
  char hbuf[9]; sprintf(hbuf, "%02d:%02d:%02d", n.hour(), n.minute(), n.second());
  tft.setCursor(190, 6); tft.print(hbuf);

  // ── Pied de page : auteur + école + date — bas à droite ──
  tft.fillRect(0, SCREEN_H - 20, SCREEN_W, 20, 0x0000);
  tft.setTextSize(1); tft.setTextColor(CLR_WHITE);
  // Date à gauche du pied de page
  const char* j[] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};
  char dbuf[22];
  sprintf(dbuf, "%s %02d/%02d/%04d", j[n.dayOfTheWeek()], n.day(), n.month(), n.year());
  tft.setCursor(COL_X, SCREEN_H - 13); tft.print(dbuf);
  // Auteur + école à droite du pied de page
  //tft.setTextColor(0x3186);
  tft.setTextColor(CLR_WHITE);
  tft.setCursor(180, SCREEN_H - 13); tft.print("Jerry E.  CFPE Douala");
}

void majLiaison() {
  tft.fillRect(60, 188, 150, 10, CLR_BG);
  tft.setTextSize(1);
  if (estDeconnecte) {
    tft.setTextColor(CLR_RED);
    unsigned long s = (millis() - debutDeconnexion) / 1000;
    tft.setCursor(60, 190); tft.print("PERDUE depuis "); tft.print(s); tft.print("s");
  } else {
    tft.setTextColor(CLR_GREEN);
    tft.setCursor(60, 190); tft.print("ESP-NOW · OK");
  }
}

void dessinerInterfaceComplete(float p) {
  tft.fillScreen(CLR_BG);
  dessinerTitre();
  dessinerSeparateur();
  dessinerLabels();
  dessinerBouteille();
  majPression(p);
  majStatut(p);
  majBouteille(p);
  majHeure();
  majLiaison();
}

// =============================================================
//  PAGE WEB — HTML complet (portail captif + dashboard)
// =============================================================

/*
 * Construit le JSON de données temps réel (appelé par /api/data)
 */
String buildJSON() {
  DateTime n = obtenirHeure();
  const char* j[] = {"Dim","Lun","Mar","Mer","Jeu","Ven","Sam"};
  char hbuf[9], dbuf[16];
  sprintf(hbuf, "%02d:%02d:%02d", n.hour(), n.minute(), n.second());
  sprintf(dbuf, "%s %02d/%02d/%04d", j[n.dayOfTheWeek()], n.day(), n.month(), n.year());

  float r = constrain(pression_actuelle / 2.5f, 0.0f, 1.0f);
  int pct = (int)(r * 100);
  String nvc = (r > 0.5f) ? "#00e676" : (r > 0.25f) ? "#ff9100" : "#ff3d3d";
  String sc  = (pression_actuelle >= SEUIL_NORMAL) ? "normal" :
               (pression_actuelle >= SEUIL_ATTENTION) ? "attention" : "alerte";

  String json = "{";
  json += "\"pression\":" + String(pression_actuelle, 2) + ",";
  json += "\"statut\":\"" + libelleStatut(pression_actuelle) + "\",";
  json += "\"statutClass\":\"" + sc + "\",";
  json += "\"niveau\":" + String(pct) + ",";
  json += "\"niveauCouleur\":\"" + nvc + "\",";
  json += "\"heure\":\"" + String(hbuf) + "\",";
  json += "\"date\":\"" + String(dbuf) + "\",";
  json += "\"liaison\":" + String(estDeconnecte ? "false" : "true") + ",";
  json += "\"liaisonDuree\":" + String(estDeconnecte ? (millis()-debutDeconnexion)/1000 : 0) + ",";
  json += "\"buzzerActif\":" + String(buzzerActif ? "true" : "false") + ",";
  json += "\"premierContact\":\"" + String(nbContacts > 0 ? planning[0].nom : "—") + "\"";
  json += "}";
  return json;
}

/*
 * Construit le JSON du planning de garde
 */
String buildPlanningJSON() {
  String json = "[";
  for (int i = 0; i < nbContacts; i++) {
    if (i > 0) json += ",";
    json += "{\"nom\":\"" + String(planning[i].nom) + "\",";
    json += "\"tel\":\"" + String(planning[i].telephone) + "\",";
    json += "\"poste\":\"" + String(planning[i].poste) + "\"}";
  }
  json += "]";
  return json;
}

/*
 * Construit le JSON de l'historique des alertes
 */
String buildAlertesJSON() {
  String json = "[";
  int total = min(nbAlertes, MAX_ALERTES);
  for (int i = 0; i < total; i++) {
    int idx = (nbAlertes - total + i) % MAX_ALERTES;
    if (i > 0) json += ",";
    json += "{\"dt\":\"" + String(historiqueAlertes[idx].datetime) + "\",";
    json += "\"p\":" + String(historiqueAlertes[idx].pression, 2) + ",";
    json += "\"s\":\"" + String(historiqueAlertes[idx].statut) + "\"}";
  }
  json += "]";
  return json;
}

/*
 * PAGE HTML PRINCIPALE — Dashboard complet
 * Design médical sombre, fidèle à l'écran TFT
 */
const char HTML_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>IoT-GG V1.0</title>
<style>
:root {
  --bg:#03080f; --surface:#080f1a; --card:#0c1420;
  --cyan:#00d4ff; --green:#00e676; --orange:#ff9100;
  --red:#ff3d3d; --yellow:#ffd600; --white:#e8f4f8;
  --dim:#7a9bb0; --dimmer:#3a5570; --purple:#7c4dff;
  --font:'Courier New',monospace;
}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--white);font-family:var(--font);min-height:100vh}
/* ── HEADER ── */
.hdr{background:#060d18;border-bottom:1px solid rgba(0,212,255,.25);
  padding:12px 20px;display:flex;align-items:center;gap:16px;flex-wrap:wrap}
.logo{width:44px;height:44px;border-radius:50%;
  background:radial-gradient(circle,rgba(0,212,255,.3),rgba(0,212,255,.05));
  border:2px solid var(--cyan);display:flex;align-items:center;
  justify-content:center;font-size:13px;font-weight:700;color:var(--cyan);
  box-shadow:0 0 18px rgba(0,212,255,.4)}
.brand{font-size:20px;font-weight:700;letter-spacing:4px;color:var(--cyan)}
.brand span{color:var(--white)}
.hdr-right{margin-left:auto;text-align:right}
.hdr-heure{font-size:22px;font-weight:700;color:var(--white);letter-spacing:2px}
.hdr-date{font-size:10px;color:var(--dim);letter-spacing:1px}
.hdr-auteur{font-size:9px;color:var(--dimmer);letter-spacing:1px;margin-top:2px}
/* ── NAV TABS ── */
.nav{display:flex;gap:0;border-bottom:1px solid rgba(255,255,255,.07);
  background:var(--surface);overflow-x:auto}
.nav-btn{padding:11px 20px;background:transparent;border:none;
  color:var(--dim);cursor:pointer;font-family:var(--font);font-size:9px;
  font-weight:700;letter-spacing:2px;border-bottom:2px solid transparent;
  white-space:nowrap;transition:all .2s}
.nav-btn.active{color:var(--cyan);border-bottom-color:var(--cyan);background:rgba(0,212,255,.05)}
.nav-btn:hover:not(.active){color:var(--white)}
/* ── MAIN ── */
.main{padding:20px;max-width:960px;margin:0 auto}
.tab-content{display:none}.tab-content.active{display:block}
/* ── GRILLE DASHBOARD ── */
.grid-2{display:grid;grid-template-columns:1fr 1fr;gap:16px}
.grid-3{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}
@media(max-width:600px){.grid-2,.grid-3{grid-template-columns:1fr}}
/* ── CARDS ── */
.card{background:var(--card);border:1px solid rgba(255,255,255,.06);
  border-radius:10px;padding:18px;position:relative;overflow:hidden}
.card-title{font-size:8px;letter-spacing:3px;color:var(--dim);margin-bottom:14px}
.card-accent{border-left:3px solid var(--cyan)}
/* ── PRESSION ── */
.pression-val{font-size:52px;font-weight:700;color:var(--yellow);
  line-height:1;letter-spacing:-1px}
.pression-unit{font-size:14px;color:var(--dim);margin-left:6px}
.pression-sub{font-size:9px;color:var(--dimmer);margin-top:4px}
/* ── STATUT BADGE ── */
.badge{display:inline-flex;align-items:center;gap:8px;padding:8px 18px;
  border-radius:6px;font-size:14px;font-weight:700;letter-spacing:1px;margin-top:12px}
.badge.normal{background:rgba(0,230,118,.15);color:var(--green);border:1px solid rgba(0,230,118,.3)}
.badge.attention{background:rgba(255,145,0,.15);color:var(--orange);border:1px solid rgba(255,145,0,.3)}
.badge.alerte{background:rgba(255,61,61,.15);color:var(--red);border:1px solid rgba(255,61,61,.3);animation:pulse 1s infinite}
@keyframes pulse{0%,100%{box-shadow:0 0 0 0 rgba(255,61,61,.4)}50%{box-shadow:0 0 0 6px rgba(255,61,61,0)}}
.dot{width:8px;height:8px;border-radius:50%}
.dot.normal{background:var(--green)}
.dot.attention{background:var(--orange)}
.dot.alerte{background:var(--red)}
/* ── BOUTEILLE SVG ── */
.bouteille-wrap{display:flex;align-items:center;gap:16px}
.bouteille-svg{flex-shrink:0}
.bouteille-pct-txt{font-size:28px;font-weight:700;color:var(--white)}
.bouteille-label{font-size:9px;color:var(--dim);letter-spacing:2px;margin-top:4px}
/* ── LIAISON ── */
.liaison-ok{color:var(--green);font-size:11px;font-weight:700;
  display:flex;align-items:center;gap:6px}
.liaison-err{color:var(--red);font-size:11px;font-weight:700;
  display:flex;align-items:center;gap:6px;animation:pulse 1s infinite}
.liaison-dot{width:8px;height:8px;border-radius:50%;flex-shrink:0}
/* ── BOUTON ACQUITTEMENT ── */
.btn-acquitter{width:100%;padding:12px;background:var(--red);color:#fff;
  border:none;border-radius:6px;font-family:var(--font);font-size:11px;
  font-weight:700;letter-spacing:2px;cursor:pointer;margin-top:14px;transition:.2s}
.btn-acquitter:hover{opacity:.85}
.btn-acquitter:disabled{background:var(--dimmer);cursor:not-allowed;opacity:.5}
/* ── PLANNING ── */
.planning-table{width:100%;border-collapse:collapse;font-size:11px}
.planning-table th{padding:8px 10px;text-align:left;color:var(--cyan);
  font-size:8px;letter-spacing:2px;background:rgba(0,212,255,.05);
  border-bottom:1px solid rgba(0,212,255,.2)}
.planning-table td{padding:10px 10px;border-bottom:1px solid rgba(255,255,255,.04);
  vertical-align:middle}
.planning-table tr:hover td{background:rgba(255,255,255,.02)}
.tel-link{color:var(--cyan);text-decoration:none;font-weight:700}
.tel-link:hover{text-decoration:underline}
.badge-poste{background:rgba(124,77,255,.15);color:var(--purple);
  padding:2px 8px;border-radius:3px;font-size:9px;letter-spacing:1px}
.badge-actif{background:rgba(0,230,118,.1);color:var(--green);
  padding:2px 8px;border-radius:3px;font-size:9px}
/* ── FORM PLANNING ── */
.form-grid{display:grid;grid-template-columns:1fr 1fr 1fr auto;gap:10px;margin-top:14px}
@media(max-width:600px){.form-grid{grid-template-columns:1fr}}
.input-field{background:#080f1a;border:1px solid rgba(255,255,255,.12);
  border-radius:5px;padding:9px 12px;color:var(--white);font-family:var(--font);
  font-size:11px;outline:none;transition:.2s;width:100%}
.input-field:focus{border-color:var(--cyan);box-shadow:0 0 0 2px rgba(0,212,255,.1)}
.btn{padding:9px 18px;border:none;border-radius:5px;cursor:pointer;
  font-family:var(--font);font-size:9px;font-weight:700;letter-spacing:1.5px;transition:.2s}
.btn-primary{background:var(--cyan);color:#03080f}
.btn-primary:hover{opacity:.85}
.btn-danger{background:rgba(255,61,61,.15);color:var(--red);
  border:1px solid rgba(255,61,61,.3)}
.btn-danger:hover{background:rgba(255,61,61,.25)}
.btn-sync{background:rgba(0,230,118,.1);color:var(--green);
  border:1px solid rgba(0,230,118,.3);width:100%;margin-top:10px}
/* ── ALERTES ── */
.alerte-item{display:flex;align-items:center;gap:12px;padding:10px 12px;
  border-radius:6px;margin-bottom:8px;background:rgba(255,255,255,.03)}
.alerte-dt{font-size:10px;color:var(--dim);min-width:120px}
.alerte-p{font-size:13px;font-weight:700;color:var(--yellow);min-width:70px}
.alerte-s{font-size:10px;font-weight:700;padding:2px 8px;border-radius:3px}
.alerte-s.normal{background:rgba(0,230,118,.1);color:var(--green)}
.alerte-s.attention{background:rgba(255,145,0,.1);color:var(--orange)}
.alerte-s.alerte{background:rgba(255,61,61,.1);color:var(--red)}
.empty-state{text-align:center;padding:40px;color:var(--dimmer);font-size:12px}
/* ── RTC SYNC ── */
.sync-row{display:flex;gap:10px;align-items:center;margin-top:14px;flex-wrap:wrap}
.sync-input{flex:1;min-width:200px}
/* ── SEPARATEUR SECTION ── */
.section-title{font-size:9px;color:var(--cyan);letter-spacing:3px;
  margin-bottom:14px;padding-left:10px;border-left:3px solid var(--cyan)}
.divider{height:1px;background:rgba(255,255,255,.06);margin:20px 0}
</style>
</head>
<body>

<!-- HEADER -->
<div class="hdr">
  <div class="logo">O₂</div>
  <div>
    <div class="brand">IoT<span>-Gas Guard v1.0</span></div>
    <div style="font-size:9px;color:var(--dimmer);letter-spacing:1px">SALLE DE GARDE</div>
  </div>
  <div class="hdr-right">
    <div class="hdr-heure" id="hdr-heure">--:--:--</div>
    <div class="hdr-date" id="hdr-date">---</div>
    <div class="hdr-auteur">Jerry Essogo · CFPE Douala · DTS 2025-2026</div>
  </div>
</div>

<!-- NAVIGATION -->
<div class="nav">
  <button class="nav-btn active" onclick="showTab('dashboard')">TABLEAU DE BORD</button>
  <button class="nav-btn" onclick="showTab('planning')">PLANNING DE GARDE</button>
  <button class="nav-btn" onclick="showTab('alertes')">HISTORIQUE ALERTES</button>
  <button class="nav-btn" onclick="showTab('reglages')">RÉGLAGES</button>
</div>

<!-- ═══ TAB 1 : DASHBOARD ═══ -->
<div class="main">
<div id="tab-dashboard" class="tab-content active">
  <div class="grid-2" style="margin-bottom:16px">

    <!-- Carte pression -->
    <div class="card card-accent">
      <div class="card-title">PRESSION OXYGÈNE ACTUELLE</div>
      <div style="display:flex;align-items:baseline;gap:6px">
        <div class="pression-val" id="pression-val">--</div>
        <div class="pression-unit">bars</div>
      </div>
      <div class="pression-sub">Post-détendeur · Capteur MPX4250AP</div>
      <div id="statut-badge" class="badge normal">
        <div id="statut-dot" class="dot normal"></div>
        <span id="statut-txt">NORMAL</span>
      </div>
      <button class="btn-acquitter" id="btn-acquitter" disabled onclick="acquitter()">
        ACQUITTER L'ALARME
      </button>
    </div>

    <!-- Jauge bouteille -->
    <div class="card">
      <div class="card-title">NIVEAU ESTIMÉ</div>
      <div class="bouteille-wrap">
        <svg class="bouteille-svg" width="70" height="180" viewBox="0 0 70 180">
          <!-- Col -->
          <rect x="23" y="0" width="24" height="16" rx="3" fill="#2104" stroke="#4208"/>
          <rect x="23" y="13" width="24" height="5" fill="#2104"/>
          <!-- Corps -->
          <rect x="2" y="18" width="66" height="158" rx="7" fill="#2104" stroke="#4208"/>
          <!-- Graduations -->
          <line x1="8" y1="57" x2="62" y2="57" stroke="#4208" stroke-width="1" stroke-dasharray="3,3"/>
          <line x1="8" y1="97" x2="62" y2="97" stroke="#4208" stroke-width="1" stroke-dasharray="3,3"/>
          <line x1="8" y1="137" x2="62" y2="137" stroke="#4208" stroke-width="1" stroke-dasharray="3,3"/>
          <!-- Niveau dynamique -->
          <clipPath id="clip-corps">
            <rect x="4" y="20" width="62" height="154" rx="5"/>
          </clipPath>
          <rect id="svg-niveau" x="4" y="20" width="62" height="0" rx="4" fill="#00e676" clip-path="url(#clip-corps)"/>
          <!-- Reflet -->
          <rect id="svg-reflet" x="10" y="22" width="50" height="2" rx="1" fill="rgba(255,255,255,.15)"/>
          <!-- Label -->
          <text x="35" y="168" text-anchor="middle" fill="white" font-size="12" font-family="'Courier New',monospace" font-weight="700" id="svg-pct">--%</text>
        </svg>
        <div>
          <div class="bouteille-pct-txt" id="bouteille-pct">--%</div>
          <div class="bouteille-label">NIVEAU</div>
          <div style="margin-top:16px;font-size:9px;color:var(--dimmer)">
            <div>● &gt;50% → Normal</div>
            <div style="margin-top:4px;color:var(--orange)">● 25-50% → Attention</div>
            <div style="margin-top:4px;color:var(--red)">● &lt;25% → Critique</div>
          </div>
        </div>
      </div>
    </div>

  </div>

  <!-- Stats secondaires -->
  <div class="grid-3" style="margin-bottom:16px">
    <div class="card">
      <div class="card-title">LIAISON ESP-NOW</div>
      <div id="liaison-status" class="liaison-ok">
        <div class="liaison-dot" style="background:var(--green)"></div>
        Connexion active
      </div>
    </div>
    <div class="card">
      <div class="card-title">SEUILS CONFIGURÉS</div>
      <div style="font-size:11px;color:var(--dim)">
        <div>🔴 Alerte : &lt; <span style="color:var(--red)">1.00</span> bar</div>
        <div style="margin-top:6px">🟠 Attention : &lt; <span style="color:var(--orange)">1.50</span> bar</div>
      </div>
    </div>
    <div class="card">
      <div class="card-title">GARDE ACTUELLE</div>
      <div id="garde-nom" style="font-size:13px;font-weight:700;color:var(--white)">—</div>
      <div style="font-size:9px;color:var(--dim);margin-top:4px">Responsable de garde</div>
    </div>
  </div>

</div><!-- /tab-dashboard -->

<!-- ═══ TAB 2 : PLANNING ═══ -->
<div id="tab-planning" class="tab-content">
  <div class="section-title">CONTACTS DE GARDE</div>

  <div class="card" style="margin-bottom:16px">
    <table class="planning-table" id="planning-table">
      <thead>
        <tr>
          <th>#</th><th>NOM</th><th>TÉLÉPHONE</th><th>POSTE</th><th>ACTION</th>
        </tr>
      </thead>
      <tbody id="planning-tbody">
        <tr><td colspan="5" class="empty-state">Aucun contact enregistré</td></tr>
      </tbody>
    </table>
  </div>

  <div class="card">
    <div class="card-title">AJOUTER UN CONTACT</div>
    <div class="form-grid">
      <input class="input-field" id="f-nom" placeholder="Nom complet" type="text">
      <input class="input-field" id="f-tel" placeholder="+237 6XX XXX XXX" type="tel">
      <input class="input-field" id="f-poste" placeholder="Poste / Fonction" type="text">
      <button class="btn btn-primary" onclick="ajouterContact()">+ AJOUTER</button>
    </div>
    <div style="margin-top:10px;font-size:9px;color:var(--dimmer)">
      ⚠ Le premier contact de la liste est appelé en priorité lors d'une alerte GSM.
    </div>
  </div>
</div><!-- /tab-planning -->

<!-- ═══ TAB 3 : ALERTES ═══ -->
<div id="tab-alertes" class="tab-content">
  <div class="section-title">HISTORIQUE DES 20 DERNIÈRES ALERTES</div>
  <div class="card">
    <div id="alertes-list">
      <div class="empty-state">Aucune alerte enregistrée</div>
    </div>
  </div>
</div><!-- /tab-alertes -->

<!-- ═══ TAB 4 : RÉGLAGES ═══ -->
<div id="tab-reglages" class="tab-content">
  <div class="section-title">SYNCHRONISATION RTC</div>
  <div class="card" style="margin-bottom:16px">
    <div style="font-size:11px;color:var(--dim);margin-bottom:12px">
      Synchronise l'horloge du module DS3231 avec l'heure de ton ordinateur.
      Cliquez sur le bouton ci-dessous — l'heure est lue depuis le navigateur.
    </div>
    <div class="sync-row">
      <input class="input-field sync-input" type="datetime-local" id="rtc-input">
      <button class="btn btn-primary" onclick="syncRTCManuel()">SYNC MANUEL</button>
    </div>
    <button class="btn btn-sync" onclick="syncRTCAuto()">
      ⟳ SYNCHRONISER AUTOMATIQUEMENT (heure navigateur)
    </button>
    <div id="sync-status" style="margin-top:10px;font-size:10px;color:var(--dim)"></div>
  </div>

  <div class="section-title">INFORMATIONS SYSTÈME</div>
  <div class="card">
    <div style="font-size:11px;color:var(--dim);line-height:2">
      <div>Capteur : <span style="color:var(--white)">MPX4250AP · 0–250 kPa</span></div>
      <div>Communication : <span style="color:var(--white)">ESP-NOW · 2.4GHz</span></div>
      <div>SSID : <span style="color:var(--cyan)">IoT-GG_AP</span></div>
      <div>Seuil alerte : <span style="color:var(--red)">1.00 bar</span></div>
      <div>Seuil attention : <span style="color:var(--orange)">1.50 bar</span></div>
      <div>Watchdog : <span style="color:var(--white)">Ping toutes les 5s</span></div>
      <div>Auteur : <span style="color:var(--white)">ESSOGO NGAMO JERRY ORLAN</span></div>
      <div>École : <span style="color:var(--white)">CFPE Douala · DTS 2025-2026</span></div>
    </div>
  </div>
</div><!-- /tab-reglages -->

</div><!-- /main -->

<script>
// ── Navigation tabs ──────────────────────────────────────────
function showTab(id) {
  document.querySelectorAll('.tab-content').forEach(t => t.classList.remove('active'));
  document.querySelectorAll('.nav-btn').forEach(b => b.classList.remove('active'));
  document.getElementById('tab-' + id).classList.add('active');
  event.target.classList.add('active');
  if (id === 'alertes') chargerAlertes();
  if (id === 'planning') chargerPlanning();
}

// ── Mise à jour données temps réel ──────────────────────────
function majDashboard(d) {
  // Heure + date dans le header
  document.getElementById('hdr-heure').textContent = d.heure;
  document.getElementById('hdr-date').textContent  = d.date;

  // Pression
  document.getElementById('pression-val').textContent = d.pression;

  // Statut badge
  const badge = document.getElementById('statut-badge');
  const dot   = document.getElementById('statut-dot');
  const txt   = document.getElementById('statut-txt');
  badge.className = 'badge ' + d.statutClass;
  dot.className   = 'dot ' + d.statutClass;
  txt.textContent = d.statut;

  // Bouteille SVG
  const maxH = 154, topY = 20;
  const fillH = Math.round((d.niveau / 100) * maxH);
  const fillY = topY + maxH - fillH;
  const nivel = document.getElementById('svg-niveau');
  const reflet = document.getElementById('svg-reflet');
  nivel.setAttribute('y', fillY);
  nivel.setAttribute('height', fillH);
  nivel.setAttribute('fill', d.niveauCouleur);
  if (fillH > 4) { reflet.setAttribute('y', fillY + 2); reflet.style.display = 'block'; }
  else reflet.style.display = 'none';
  document.getElementById('svg-pct').textContent = d.niveau + '%';
  document.getElementById('bouteille-pct').textContent = d.niveau + '%';
  document.getElementById('bouteille-pct').style.color = d.niveauCouleur;

  // Liaison
  const liaisonDiv = document.getElementById('liaison-status');
  if (d.liaison) {
    liaisonDiv.className = 'liaison-ok';
    liaisonDiv.innerHTML = '<div class="liaison-dot" style="background:var(--green)"></div> Connexion active';
  } else {
    liaisonDiv.className = 'liaison-err';
    liaisonDiv.innerHTML = '<div class="liaison-dot" style="background:var(--red)"></div> PERDUE depuis ' + d.liaisonDuree + 's';
  }

  // Buzzer
  const btn = document.getElementById('btn-acquitter');
  btn.disabled = !d.buzzerActif;

  // Garde actuelle
  if (d.premierContact && d.premierContact !== '—')
    document.getElementById('garde-nom').textContent = d.premierContact;
}

// ── Planning ─────────────────────────────────────────────────
function chargerPlanning() {
  fetch('/api/planning').then(r => r.json()).then(data => {
    const tbody = document.getElementById('planning-tbody');
    if (data.length === 0) {
      tbody.innerHTML = '<tr><td colspan="5" class="empty-state">Aucun contact enregistré</td></tr>';
      return;
    }
    tbody.innerHTML = data.map((c, i) => `
      <tr>
        <td style="color:var(--dimmer)">${i+1}</td>
        <td style="font-weight:700">${c.nom}${i===0?'<span class="badge-actif" style="margin-left:8px">PRIORITAIRE</span>':''}</td>
        <td><a class="tel-link" href="tel:${c.tel}">${c.tel}</a></td>
        <td><span class="badge-poste">${c.poste}</span></td>
        <td><button class="btn btn-danger" onclick="supprimerContact(${i})">✕</button></td>
      </tr>`).join('');
  });
}

function ajouterContact() {
  const nom   = document.getElementById('f-nom').value.trim();
  const tel   = document.getElementById('f-tel').value.trim();
  const poste = document.getElementById('f-poste').value.trim();
  if (!nom || !tel) { alert('Nom et téléphone obligatoires'); return; }
  fetch('/api/planning/ajouter', {
    method:'POST',
    headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:`nom=${encodeURIComponent(nom)}&tel=${encodeURIComponent(tel)}&poste=${encodeURIComponent(poste)}`
  }).then(() => {
    document.getElementById('f-nom').value = '';
    document.getElementById('f-tel').value = '';
    document.getElementById('f-poste').value = '';
    chargerPlanning();
  });
}

function supprimerContact(idx) {
  if (!confirm('Supprimer ce contact ?')) return;
  fetch('/api/planning/supprimer?idx=' + idx).then(() => chargerPlanning());
}

// ── Alertes ──────────────────────────────────────────────────
function chargerAlertes() {
  fetch('/api/alertes').then(r => r.json()).then(data => {
    const div = document.getElementById('alertes-list');
    if (data.length === 0) {
      div.innerHTML = '<div class="empty-state">Aucune alerte enregistrée</div>'; return;
    }
    const reversed = [...data].reverse();
    div.innerHTML = reversed.map(a => {
      const sc = a.s.toLowerCase().includes('alerte') ? 'alerte' :
                 a.s.toLowerCase().includes('attention') ? 'attention' : 'normal';
      return `<div class="alerte-item">
        <div class="alerte-dt">${a.dt}</div>
        <div class="alerte-p">${a.p} bars</div>
        <div class="alerte-s ${sc}">${a.s}</div>
      </div>`;
    }).join('');
  });
}

// ── Acquittement ──────────────────────────────────────────────
function acquitter() {
  fetch('/api/acquitter').then(() => {
    document.getElementById('btn-acquitter').disabled = true;
  });
}

// ── Synchronisation RTC ───────────────────────────────────────
function syncRTCAuto() {
  const now = new Date();
  const fmt = `${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}T${pad(now.getHours())}:${pad(now.getMinutes())}:${pad(now.getSeconds())}`;
  envoyerSync(fmt);
}

function syncRTCManuel() {
  const val = document.getElementById('rtc-input').value;
  if (!val) { alert('Sélectionnez une date/heure'); return; }
  envoyerSync(val + ':00');
}

function envoyerSync(isoStr) {
  fetch('/api/sync-rtc?dt=' + encodeURIComponent(isoStr))
    .then(r => r.text())
    .then(msg => {
      document.getElementById('sync-status').innerHTML =
        '<span style="color:var(--green)">✓ ' + msg + '</span>';
    });
}

function pad(n) { return n.toString().padStart(2,'0'); }

// ── Initialisation du champ datetime-local ──
document.addEventListener('DOMContentLoaded', () => {
  const now = new Date();
  const local = `${now.getFullYear()}-${pad(now.getMonth()+1)}-${pad(now.getDate())}T${pad(now.getHours())}:${pad(now.getMinutes())}`;
  document.getElementById('rtc-input').value = local;
});

// ── Polling temps réel toutes les 1.5s ───────────────────────
function fetchData() {
  fetch('/api/data')
    .then(r => r.json())
    .then(d => majDashboard(d))
    .catch(() => {});
}
fetchData();
setInterval(fetchData, 1500);
</script>
</body>
</html>
)rawhtml";

// =============================================================
//  HANDLERS SERVEUR WEB
// =============================================================

// Portail captif : redirige TOUT vers la page principale
void handleCaptivePortal() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void handleRoot() {
  server.send_P(200, "text/html", HTML_PAGE);
}

void handleApiData() {
  server.send(200, "application/json", buildJSON());
}

void handleApiAlertes() {
  server.send(200, "application/json", buildAlertesJSON());
}

void handleApiPlanning() {
  server.send(200, "application/json", buildPlanningJSON());
}

void handleApiAjouterContact() {
  if (nbContacts >= MAX_CONTACTS) { server.send(400, "text/plain", "Planning plein"); return; }
  String nom   = server.arg("nom");
  String tel   = server.arg("tel");
  String poste = server.arg("poste");
  strncpy(planning[nbContacts].nom,       nom.c_str(),   31);
  strncpy(planning[nbContacts].telephone, tel.c_str(),   15);
  strncpy(planning[nbContacts].poste,     poste.c_str(), 31);
  planning[nbContacts].actif = true;
  nbContacts++;
  sauvegarderPlanning();
  server.send(200, "text/plain", "OK");
  logSerie(); Serial.printf("✓ Contact ajouté : %s %s\n", nom.c_str(), tel.c_str());
}

void handleApiSupprimerContact() {
  int idx = server.arg("idx").toInt();
  if (idx < 0 || idx >= nbContacts) { server.send(400, "text/plain", "Index invalide"); return; }
  for (int i = idx; i < nbContacts - 1; i++) planning[i] = planning[i+1];
  nbContacts--;
  sauvegarderPlanning();
  server.send(200, "text/plain", "OK");
}

void handleApiAcquitter() {
  arreterBuzzer(); buzzerAcquitte = true; t_acquitt = millis();
  logSerie(); Serial.println(">>> Buzzer acquitté via portail web");
  server.send(200, "text/plain", "OK");
}

void handleApiSyncRTC() {
  String dt = server.arg("dt");
  syncRTC(dt);
  server.send(200, "text/plain", "RTC synchronisée → " + dt);
}

// =============================================================
//  CALLBACK ESP-NOW
// =============================================================
void onDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(msg_pression)) {
    memcpy(&dataRecue, incomingData, sizeof(dataRecue));
    pression_actuelle = dataRecue.pression;
    premiereReception = true;
    logSerie();
    Serial.printf("📥 %.2f bars | %s | trame %u\n",
      dataRecue.pression, dataRecue.statut, dataRecue.compteur);
  }
  else if (len == sizeof(msg_ping)) {
    memcpy(&dataPongRecu, incomingData, sizeof(dataPongRecu));
    if (!dataPongRecu.estPing) {
      dernierPongRecu = millis();
      if (estDeconnecte) {
        unsigned long d = millis() - debutDeconnexion;
        logSerie(); Serial.printf("✅ Liaison rétablie (coupure %.1fs)\n", d/1000.0);
        estDeconnecte = false;
      }
    }
  }
}

// =============================================================
//  SETUP
// =============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== IoT-GG ESCLAVE v1.0 — Démarrage ===");

  // ── GPIO ───────────────────────────────────────────────────
  pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW);
  pinMode(PIN_BOUTON, INPUT_PULLUP);
  Serial.println("✓ GPIO configurés");

  // ── RTC ────────────────────────────────────────────────────
  Wire.begin(21, 22);
  if (!rtc.begin()) {
    Serial.println("⚠ RTC absent — heure de secours");
  } else {
    rtcDisponible = true;
    if (rtc.lostPower()) {
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
      Serial.println("⚠ RTC resynchronisé sur heure de compilation");
      Serial.println("  → Utilise le portail web pour ajuster précisément");
    }
    Serial.println("✓ RTC DS3231 opérationnel");
  }

  // LED RGB--------------------
  initLED();

  // ── TFT ────────────────────────────────────────────────────
  tft.begin(); tft.setRotation(1);
  Serial.println("✓ TFT initialisé");

  // ── SD ─────────────────────────────────────────────────────
  if (SD.begin(SD_CS)) {
    Serial.println("✓ SD Card opérationnelle");
    chargerPlanning();
  } else {
    Serial.println("⚠ SD non détectée — planning en mémoire uniquement");
  }

  // ── WiFi AP + Portail captif ────────────────────────────────
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255,255,255,0));
  WiFi.softAP(ssidAP, passwordAP);
  Serial.printf("✓ AP démarré — SSID: %s\n", ssidAP);
  Serial.printf("  IP: %s\n", WiFi.softAPIP().toString().c_str());

  // DNS : redirige tout vers 192.168.4.1 → portail captif
  dnsServer.start(DNS_PORT, "*", apIP);
  Serial.println("✓ DNS captif démarré");

  // ── Routes serveur web ─────────────────────────────────────
  server.on("/",                    handleRoot);
  server.on("/generate_204",        handleCaptivePortal);  // Android
  server.on("/hotspot-detect.html", handleCaptivePortal);  // iOS
  server.on("/ncsi.txt",            handleCaptivePortal);  // Windows
  server.on("/api/data",            handleApiData);
  server.on("/api/alertes",         handleApiAlertes);
  server.on("/api/planning",        handleApiPlanning);
  server.on("/api/planning/ajouter",  HTTP_POST, handleApiAjouterContact);
  server.on("/api/planning/supprimer",handleApiSupprimerContact);
  server.on("/api/acquitter",       handleApiAcquitter);
  server.on("/api/sync-rtc",        handleApiSyncRTC);
  server.onNotFound(handleCaptivePortal); // Tout le reste → portail captif
  server.begin();
  Serial.println("✓ Serveur web démarré");

  // ── ESP-NOW ────────────────────────────────────────────────
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Erreur init ESP-NOW");
  } else {
    esp_now_register_recv_cb(onDataRecv);
    memcpy(peerInfo.peer_addr, adresseMacMaitre, 6);
    peerInfo.channel = 0; peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
    Serial.println("✓ ESP-NOW initialisé");
  }
  dernierPongRecu = millis();

  // ── Interface TFT ─────────────────────────────────────────
  dessinerInterfaceComplete(pression_actuelle);
  Serial.println("=== Prêt ===\n");
}

// =============================================================
//  LOOP
// =============================================================
unsigned long t_heure = 0, t_liaison = 0;

void loop() {
  unsigned long now = millis();

  // 1. Portail captif DNS + serveur web
  dnsServer.processNextRequest();
  server.handleClient();

  // 2. Ping watchdog
  if (now - dernierPingEnvoye >= INTERVALLE_PING) {
    dernierPingEnvoye = now;
    dataPing.estPing = true; dataPing.timestamp = now;
    esp_now_send(adresseMacMaitre, (uint8_t*)&dataPing, sizeof(dataPing));
  }

  // 3. Détection déconnexion
  if (now - dernierPongRecu > TIMEOUT_DECONNEXION && !estDeconnecte) {
    estDeconnecte = true; debutDeconnexion = dernierPongRecu;
    logSerie(); Serial.println("⚠ DÉCONNEXION du nœud maître !");
  }

  // 4. Gère la LED selon le statut
  gererLED(pression_actuelle);



  // 5. Mise à jour TFT si changement
  if (premiereReception) {
    bool chanP = abs(pression_actuelle - pression_prev) > 0.01f;
    String nvS = libelleStatut(pression_actuelle);
    bool chanS = (nvS != statut_prev);

    if (chanP) {
      majPression(pression_actuelle);
      majBouteille(pression_actuelle);
      pression_prev = pression_actuelle;
    }
    if (chanS) {
      String ancS = statut_actuel;
      statut_actuel = nvS; statut_prev = nvS;
      bool nvAnorm = (nvS == "ATTENTION" || nvS == "ALERTE CRITIQUE");
      bool ancAnorm = (ancS == "ATTENTION" || ancS == "ALERTE CRITIQUE");
      if (nvAnorm && !buzzerAcquitte) {
        demarrerBuzzer();
        enregistrerAlerte(pression_actuelle, nvS.c_str());
      } else if (!nvAnorm && ancAnorm) {
        arreterBuzzer(); buzzerAcquitte = false;
        logSerie(); Serial.println("✓ Retour normal");
      }
    }
  }

  // 6. Statut TFT (clignotement)
  majStatut(pression_actuelle);

  // 7. Bouton + re-déclenchement
  gererBouton();
  verifierRedeclenchement();

  // 8. Buzzer PWM
  gererBuzzer();

  // 9. Heure TFT (toutes les secondes)
  if (now - t_heure >= 1000) { t_heure = now; majHeure(); }

  // 10. Liaison TFT (toutes les secondes)
  if (now - t_liaison >= 1000) { t_liaison = now; majLiaison(); }
}
