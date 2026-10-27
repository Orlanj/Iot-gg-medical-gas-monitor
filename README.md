[![Build Status](badge)](link) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) [![Platform: ESP32](badge)] [![PRs Welcome](badge)]

**Languages:** [English](README.md) | [Français](README.fr.md)
# IoT-GG Medical Gas Monitor — V1.0
### Plateforme intelligente de supervision des réseaux de gaz médicaux hospitaliers
### Module Oxygène (MVP) · Conçu à partir de l'étude architecturale de l'HGOPED Douala

> © 2026 ESSOGO NGAMO Jerry Orlan — Tous droits réservés / All rights reserved.
> Projet soutenu au CFPE Douala — DTS Électronique Industrielle 2025-2026 — **Mention Excellente**.
> Licence : MIT (code) · CC BY-NC-SA 4.0 (documentation & schémas)

![version](https://img.shields.io/badge/version-V1.0-blue)
![license](https://img.shields.io/badge/license-MIT-green)
![mcu](https://img.shields.io/badge/MCU-ESP32--WROOM--32-red)
![origin](https://img.shields.io/badge/case_study-HGOPED_Douala-orange)

---

##  Vision & origine / Vision & origin

**IoT-GG** est une plateforme unifiée de supervision en temps réel des gaz médicaux
hospitaliers (O₂, air médical, N₂O, vide). Son architecture matérielle et logicielle a
été modélisée à partir d'une **étude de terrain des réseaux de fluides de l'HGOPED
Douala** (Cameroun), en intégrant les contraintes réelles des établissements de santé
d'Afrique subsaharienne : bascules secteur/groupe électrogène, coupures réseau,
zones techniques d'accès restreint.

**La V1.0 (ce dépôt)** est la preuve de concept validée sur le réseau d'**oxygène
médical** — le fluide le plus critique en maternité et réanimation. L'architecture
modulaire permettra le déploiement aux autres gaz sans redesign matériel (roadmap V2.0).

**IoT-GG** is a unified real-time monitoring platform for hospital medical gases
(O₂, medical air, N₂O, vacuum). Its hardware and software architecture was modelled
from a **field study of the medical fluid networks of HGOPED Douala** (Cameroon),
addressing real sub-Saharan healthcare constraints: generator changeover gaps,
network outages, restricted technical areas. **V1.0 (this repository)** is the
proof-of-concept validated on the **medical oxygen** network — the most critical
fluid in maternity and neonatal care.

---

## Fonctionnalités V1.0 (module Oxygène)

- **Mesure robuste** : pression post-détendeur (MPX4250AP, moyenne de 50 lectures) ;
  architecture hospitalière prête pour un 2ᵉ capteur haute pression 4-20 mA (0-250 bar)
  avec contrôle croisé (détection de dérive capteur).
- **Transmission redondante** : LoRa 433 MHz (principal, traverse le béton) +
  ESP-NOW 2,4 GHz (secours), watchdog ping/pong 5 s (détection de coupure < 12 s).
- **Alertes multi-canaux** : écran TFT ILI9341, buzzer acquittable (bouton ou web),
  LED RGB 3 niveaux, **SMS + appel vocal GSM (SIM800L)** au responsable de garde.
- **SMS automatiques** : « ALERTE CRITIQUE!!! NIVEAU DE PRESSION: X.XX bars » et
  « Défaillance système : perte de connexion maître-esclave » (après 60 s),
  avec anti-spam (1 SMS/min) et réarmement au retour à la normale.
- **Résilience électrique** : intégration à l'inverseur automatique
  secteur ⇄ groupe électrogène de l'hôpital + batterie tampon ~45 min
  (trou de bascule) + détection « défaut groupe » (SMS si absence > 2 min).
- **Portail web captif** : dashboard temps réel, planning de garde (carte SD),
  historique des 20 dernières alertes, synchronisation RTC depuis le navigateur.
- **Traçabilité** : journalisation horodatée des alertes sur carte SD (`alertes.csv`).

---

##  Synoptique

```
 ┌────────────────   ┌────────────────   ┌────────────────┐   ┌────────────────┐
 │ MEASURE        │   │ TRANSMIT       │   │ ALERT          │   │ ACT            │
 │ Storage room   │   │ Wireless       │   │ Guard room     │   │ Humans         │
 │────────────────│   │────────────────│   │────────────────│   │────────────────│
 │ O2 cylinders   │──►│ LoRa 433 MHz   │──►│ TFT display    │──►│ SMS + call     │
 │ 150 bar        │   │  (primary)     │   │ buzzer + LED   │   │ to on-duty     │
 │ ↓ 4-20 mA TX   │   │ ESP-NOW 2.4GHz │   │ SIM800L GSM    │   │ staff          │
 │ ↓ regulator    │   │  (backup)      │   │ Wi-Fi portal   │   │ + smartphone   │
 │ ↓ MPX4250AP    │   │ watchdog 5 s   │   │ SD log · RTC   │   │ dashboard      │
 │ ESP32 MASTER   │   │                │   │ ESP32 SLAVE    │   │                │
 └────────────────   └────────────────┘   └────────────────┘   └────────────────┘
 ┌───────────────────────────────────────────────────────────────────────────────┐
 │ ⚡ POWER 24/7 : MAINS ⇄ GENERATOR (hospital switch) + 45-min buffer battery    │
 │    + generator-fault detection (SMS if changeover fails > 2 min)              │
 └───────────────────────────────────────────────────────────────────────────────┘
```

---

##  Seuils d'alerte / Alert thresholds

| Niveau | Pression post-détendeur | Comportement |
|---|---|---|
| 🟢 NORMAL | ≥ 1,50 bar | LED verte fixe |
| 🟠 ATTENTION | 1,00 – 1,50 bar | LED orange clignotante lente + buzzer lent |
| 🔴 ALERTE CRITIQUE | < 1,00 bar | LED rouge rapide + buzzer rapide + **SMS + appel** |
| 📡 LIAISON PERDUE | > 60 s sans réponse | TFT + **SMS « Défaillance système »** |

---

##  Matériel requis / Hardware (BOM summary)

2× ESP32-WROOM-32 · TFT ILI9341 2,4" · RTC DS3231 · MPX4250AP ·
(option hôpital : transmetteur 4-20 mA 0-250 bar) · SIM800L + antenne ·
2× LoRa SX1276 · module SD · buzzer 5 V · 2N2222 · LED RGB anode commune ·
bouton poussoir · optocoupleur secteur · coffrets IP65 · batterie tampon 2×18650 +
BMS + TP4056 · parafoudre + filtre EMI · alim. HLK-10M05 / HLK-20M05.

---

## 📁 Structure du dépôt / Repository layout

```
iot-gg-medical-gas-monitor/
├── README.md · LICENSE · .gitignore
├── docs/          ← synoptique, fiche technique, manuel d'installation
├── hardware/      ← schémas, BOM
├── medias/
└── firmware/
    ├── Master_node/
    │   ├── archive_soutenance_2026/   ← figé (soutenance CFPE, mention Excellente)
    │   └── v1.0_hopital/
    └── Slave_node/
        ├── archive_soutenance_2026/   ← figé (soutenance CFPE, mention Excellente)
        └── v1.0_hopital/              ← + SIM800L SMS + architecture hospitalière
```

---

##  Installation & configuration / Setup

1. Installer l'IDE Arduino (ou PlatformIO) + bibliothèques :
   `Adafruit GFX`, `Adafruit ILI9341`, `RTClib`, `SD` (WebServer/DNSServer incluses).
2. Ouvrir le `.ino` du nœud et de la version voulus.
3. **Remplacer obligatoirement les placeholders** / **Replace the placeholders** :
   ```cpp
   uint8_t adresseMacMaitre[] = {0x00,0x00,0x00,0x00,0x00,0x00}; // MAC de VOTRE maître
   const char* ssidAP     = "VOTRE_SSID";
   const char* passwordAP = "VOTRE_MOT_DE_PASSE";
   ```
   Lire la MAC du maître / read the master MAC : `Serial.println(WiFi.macAddress());`
4. Téléverser, moniteur série à **115200 bauds**.
5. Se connecter au point d'accès du nœud esclave → portail captif automatique
   (sinon / otherwise : `http://192.168.4.1/`).

---

##  API du portail captif / Captive portal API

| Route | Méthode | Rôle |
|---|---|---|
| `/api/data` | GET | JSON temps réel (pression, statut, niveau, liaison…) |
| `/api/planning` | GET | Planning de garde |
| `/api/planning/ajouter` | POST | Ajout d'un contact |
| `/api/planning/supprimer` | GET | Suppression d'un contact |
| `/api/acquitter` | GET | Acquittement du buzzer |
| `/api/sync-rtc` | GET | Synchronisation de l'horloge DS3231 |

---

##  Versions & roadmap

| Tag / dossier | Contenu |
|---|---|
| `archive_soutenance_2026` | Code soutenu au CFPE Douala — **figé** (ex « v3.0 ») |
| `V1.0_hopital` | Version déployable : SIM800L SMS, double capteur, LoRa secours, secteur/groupe + batterie tampon, IP65 (ex « v4.x ») |
| **V1.1** (roadmap) | Passerelle centrale, rapports PDF direction, synchro NTP |
| **V2.0** (roadmap) | Extension aux autres gaz médicaux (air, N₂O, vide) |

---

## ⚠️ Avertissement / Disclaimer

IoT-GG est un **prototype de recherche et un projet académique**. Ce n'est **PAS un
dispositif médical certifié** (ISO 7396-1 / IEC 60601). Il ne doit en aucun cas se
substituer aux manomètres certifiés ni aux procédures humaines de surveillance en
vigueur dans l'établissement de santé.

IoT-GG is a **research prototype and academic project**. It is **NOT a certified
medical device** (ISO 7396-1 / IEC 60601) and must never replace certified pressure
gauges or human monitoring procedures in healthcare facilities.

---

##  Auteur / Author
**ESSOGO NGAMO Jerry Orlan** — Électronique Industrielle, CFPE Douala (Cameroun)
Contact : `essogojerryorlan@gmail.com` · LinkedIn : `Jerry Essogo Ngamo`

## 📄 Licence / License
Code : **MIT** — Copyright (c) 2026 ESSOGO NGAMO Jerry Orlan.
Documentation & schémas : **CC BY-NC-SA 4.0**.
