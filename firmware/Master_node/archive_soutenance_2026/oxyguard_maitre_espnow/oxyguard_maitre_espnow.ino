/*
 * ============================================================
 *  OXYGUARD — NŒUD MAÎTRE — Communication ESP-NOW
 *  Salle d'Oxygène · HGOPED Douala
 *
 *  Rôle de ce firmware :
 *    1. Lire la pression sur le capteur MPX4250AP (GPIO34)
 *    2. Envoyer la pression à l'esclave via ESP-NOW toutes les 5s
 *    3. Recevoir les pings watchdog de l'esclave toutes les 5s
 *    4. Répondre au ping pour confirmer qu'il est toujours actif
 *
 *  Auteur : ESSOGO NGAMO JERRY ORLAN
 *  CFPE Douala · DTS Électronique Industrielle 2025-2026
 * ============================================================
 *
 *  BROCHAGE :
 *    MPX4250AP Vout → Diviseur (R1=10k/R2=20k) → GPIO34 (ADC)
 *    ESP32 alimenté en 5V depuis HLK-20M05
 * ============================================================
 */

#include <esp_now.h>
#include <WiFi.h>

// ─────────────────────────────────────────────────────────────
//  ⚠️ REMPLACE PAR L'ADRESSE MAC RÉELLE DE TON ESP32 ESCLAVE
//  Pour l'obtenir : uploade le code "lire_mac_address.ino"
//  fourni plus bas sur l'ESP32 esclave et copie l'adresse
// ─────────────────────────────────────────────────────────────
uint8_t adresseMacEsclave[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
// Exemple réel : {0x24, 0x6F, 0x28, 0xAB, 0xCD, 0xEF}

// ─────────────────────────────────────────────────────────────
//  BROCHE CAPTEUR
// ─────────────────────────────────────────────────────────────
#define PIN_ADC 34
const int NUM_READINGS = 50;  // Moyenne pour réduire le bruit

// ─────────────────────────────────────────────────────────────
//  STRUCTURE DE DONNÉES ENVOYÉE (doit être IDENTIQUE côté esclave)
// ─────────────────────────────────────────────────────────────
typedef struct struct_message_pression {
  float   pression;       // Pression en bars
  char    statut[16];     // "NORMAL" / "ATTENTION" / "ALERTE"
  uint32_t compteur;       // Numéro de trame (pour debug)
} struct_message_pression;

typedef struct struct_message_ping {
  bool     estPing;        // true = ping watchdog, false = pong réponse
  uint32_t timestamp;       // millis() au moment de l'envoi
} struct_message_ping;

struct_message_pression dataPression;
struct_message_ping     dataPing;
struct_message_ping     dataPong;

// ─────────────────────────────────────────────────────────────
//  VARIABLES GLOBALES
// ─────────────────────────────────────────────────────────────
esp_now_peer_info_t peerInfo;

unsigned long dernierEnvoiPression = 0;
const unsigned long INTERVALLE_PRESSION = 5000; // 5 secondes

unsigned long dernierPingRecu = 0;
const unsigned long TIMEOUT_WATCHDOG = 15000; // 15s = 3 pings ratés
bool liaisonEsclaveOK = true;

uint32_t compteurTrames = 0;

// ─────────────────────────────────────────────────────────────
//  SEUILS DE PRESSION
// ─────────────────────────────────────────────────────────────
#define SEUIL_ALERTE    1.00f
#define SEUIL_ATTENTION 1.50f

// ─────────────────────────────────────────────────────────────
//  LECTURE PRESSION
// ─────────────────────────────────────────────────────────────
float lirePression() {
  long somme = 0;
  for (int i = 0; i < NUM_READINGS; i++) {
    somme += analogRead(PIN_ADC);
    delay(2);
  }
  float moy     = somme / (float)NUM_READINGS;
  float tension = (moy / 4095.0f) * 3.3f * 1.5f;
  float kPa     = ((tension / 5.0f) + 0.04f) / 0.00369f;
  return kPa / 100.0f;
}

String determinerStatut(float p) {
  if (p >= SEUIL_ATTENTION) return "NORMAL";
  if (p >= SEUIL_ALERTE)    return "ATTENTION";
  return "ALERTE";
}

// ─────────────────────────────────────────────────────────────
//  CALLBACK — Confirmation d'envoi ESP-NOW
// ─────────────────────────────────────────────────────────────
// Nouveau format pour ESP32 core 3.x
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status != ESP_NOW_SEND_SUCCESS) {
    Serial.println("⚠ Echec envoi ESP-NOW");
  }
}

// ─────────────────────────────────────────────────────────────
//  CALLBACK — Réception de données ESP-NOW
//  Reçoit les pings watchdog de l'esclave
// ─────────────────────────────────────────────────────────────
// Nouveau format pour ESP32 core 3.x
void onDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len == sizeof(struct_message_ping)) {
    memcpy(&dataPing, incomingData, sizeof(dataPing));
    
    if (dataPing.estPing) {
      Serial.println("📡 Ping reçu de l'esclave"); // Message demandé
      dernierPingRecu = millis();
      
      // Si on était en alerte, on rétablit la liaison
      if (!liaisonEsclaveOK) {
        Serial.println("✅ Liaison rétablie avec l'esclave.");
        liaisonEsclaveOK = true;
      }

      dataPong.estPing = false; 
      dataPong.timestamp = millis();
      esp_now_send(adresseMacEsclave, (uint8_t *)&dataPong, sizeof(dataPong));
    }
  }
}

// ─────────────────────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== OXYGUARD - NŒUD MAÎTRE ===");
  Serial.println("Salle d'Oxygene - CFPE Douala");

  // ── ADC ────────────────────────────────────────────────────
  analogSetAttenuation(ADC_11db);// Le commentaire ci vient...

  // ── WiFi en mode station (requis pour ESP-NOW) ─────────────
  WiFi.mode(WIFI_STA);

  // ── Affiche notre propre adresse MAC (utile pour debug) ────
  Serial.print("Adresse MAC de ce module (MAITRE) : ");
  Serial.println(WiFi.macAddress());

  // ── Initialisation ESP-NOW ──────────────────────────────────
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ Erreur init ESP-NOW");
    return;
  }
  Serial.println("✓ ESP-NOW initialise");

  // ── Enregistrement des callbacks ────────────────────────────
  esp_now_register_send_cb((esp_now_send_cb_t)onDataSent);
  esp_now_register_send_cb((esp_now_send_cb_t)onDataSent);
  esp_now_register_recv_cb((esp_now_recv_cb_t)onDataRecv);

  // ── Enregistrement du peer (esclave) ────────────────────────
  memcpy(peerInfo.peer_addr, adresseMacEsclave, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ Echec ajout du peer esclave");
    return;
  }
  Serial.println("✓ Peer esclave enregistre");
  Serial.println("=== Pret - Boucle principale ===\n");

  dernierPingRecu = millis(); // Initialise pour éviter fausse alerte au démarrage
}

// ─────────────────────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────────────────────
void loop() {
  unsigned long maintenant = millis();

  // ──────────────────────────────────────────────────────────
  //  ENVOI PRESSION — toutes les 5 secondes
  // ──────────────────────────────────────────────────────────
  if (maintenant - dernierEnvoiPression >= INTERVALLE_PRESSION) {
    dernierEnvoiPression = maintenant;

    float p = lirePression();
    String statut = determinerStatut(p);

    dataPression.pression = p;
    strncpy(dataPression.statut, statut.c_str(), sizeof(dataPression.statut));// strncpy??
    dataPression.compteur = compteurTrames++;

    esp_err_t result = esp_now_send(adresseMacEsclave,
                                     (uint8_t *)&dataPression,
                                     sizeof(dataPression));

    Serial.print("📤 Envoi #");
    Serial.print(dataPression.compteur);
    Serial.print(" | Pression: ");
    Serial.print(p, 3);
    Serial.print(" bars | Statut: ");
    Serial.print(statut);
    Serial.println(result == ESP_OK ? " | OK" : " | ECHEC");
  }

  // ──────────────────────────────────────────────────────────
  //  VÉRIFICATION WATCHDOG PROGRESSIVE
  // ──────────────────────────────────────────────────────────
  unsigned long tempsPerdu = maintenant - dernierPingRecu;
  
  if (tempsPerdu > 15000 && tempsPerdu < 15500) {
    if (liaisonEsclaveOK) {
      liaisonEsclaveOK = false;
      Serial.println("⚠️ ALERTE : Esclave injoignable depuis 15s !");
    }
  }
  
  // Affiche un rappel toutes les 45 secondes si toujours déconnecté
  if (tempsPerdu > 45000 && (tempsPerdu % 45000 < 500)) {
     Serial.println("⚠️ ALERTE : Esclave toujours injoignable depuis 45s !");
  }
}
/*
 * ============================================================
 *  POUR OBTENIR L'ADRESSE MAC DE CET ESP32 (à donner au code
 *  de l'esclave), upload temporairement ce code :
 *
 *  #include <WiFi.h>
 *  void setup() {
 *    Serial.begin(115200);
 *    WiFi.mode(WIFI_MODE_STA);
 *    Serial.println(WiFi.macAddress());
 *  }
 *  void loop() {}
 * ============================================================
 */
