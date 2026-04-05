# OFM-WasteCalendarModule

OpenKNX Modul zur Abfrage von Abfallkalender-Terminen über ICS/iCal-URLs.

## Features

- Kalender-Abonnement via konfigurierbarer ICS-URL pro Kanal
- Bis zu 4 Abfallarten pro Kanal, jede mit konfigurierbarem Suchbegriff
- Gruppenobjekte pro Abfallart:
  - Tage bis nächste Abholung (DPT 5.010)
  - Bezeichnung (DPT 16.001)
  - Abholung heute (DPT 1.001)
  - Abholung morgen (DPT 1.001)
- Tägliche automatische Aktualisierung
- Keine API-Schlüssel erforderlich

## Voraussetzungen

- ESP32 (WiFi oder Ethernet)
- ICS-URL des lokalen Abfallentsorgers

## Konfiguration (ETS)

| Parameter | Beschreibung |
|---|---|
| ICS-URL | URL zum iCal-Kalender des Anbieters |
| Suchbegriff Abfallart 1–4 | Schlüsselwort das in SUMMARY des VEVENT enthalten sein muss |

## Lizenz

GPL-3.0 – siehe [LICENSE](LICENSE)
