v0.1.0

* Feature: Unterstützung für app.abfallplus.de (11-Schritt-Wizard, gzip-Dekomprimierung)
* Feature: Unterstützung für müll.io
* Feature: Abfallart-Namen werden korrekt in KO-Strings geschrieben (UTF-8 → Latin-1 Konvertierung für ETS-Anzeige)
* Feature: Konfigurierbarer Abrufzeitpunkt (ETS-Parameter, 0–23 Uhr, Standard: 2 Uhr)
* Feature: ESP32 – Datenabruf läuft in FreeRTOS-Background-Task (kein WDT-Reset, KNX-Loop nicht blockiert)
* Feature: RP2040 – Hinweistext in ETS zu ~20s Blockierung der KNX-Kommunikation während des Datenabrufs
* Feature: Initiale Implementierung (ICS/iCal-Parser, 4 Abfallarten pro Kanal)
