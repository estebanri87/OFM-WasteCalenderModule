v0.2.0

* Change: Kanalauswahl nach OpenKNX-Standard – eigener Tab "Kanalauswahl" mit einer Zeile je Kanal (Kanal / Datenquelle / Beschreibung)
* Change: Der Schieberegler "Aktive Kalender" und der Tab "(mehr)" entfallen; ein Kanal wird über "Deaktiviert" bei der Datenquelle abgeschaltet
* Change: Deaktivierte Kanäle werden nicht mehr angelegt und erscheinen nicht in der Baumansicht; die Beschreibung bleibt trotzdem eingebbar
* Change: "Bezeichnung" heißt jetzt durchgängig "Beschreibung"
* Breaking: Die Datenquelle ist umnummeriert (0=Deaktiviert, 1=ICS-URL, 2=Apps by Abfall+, 3=müll.io). Bestehende Projekte lesen dadurch eine falsche Datenquelle und müssen neu parametriert werden
* Breaking: Das Speicherlayout verschiebt sich, da der Kanalzähler entfällt
* Fix: Ein neu angelegter Kanal ist standardmäßig "Deaktiviert" statt "ICS-URL"

v0.1.0

* Feature: Unterstützung für app.abfallplus.de (11-Schritt-Wizard, gzip-Dekomprimierung)
* Feature: Unterstützung für müll.io
* Feature: Abfallart-Namen werden korrekt in KO-Strings geschrieben (UTF-8 → Latin-1 Konvertierung für ETS-Anzeige)
* Feature: Konfigurierbarer Abrufzeitpunkt (ETS-Parameter, 0–23 Uhr, Standard: 2 Uhr)
* Feature: ESP32 – Datenabruf läuft in FreeRTOS-Background-Task (kein WDT-Reset, KNX-Loop nicht blockiert)
* Feature: RP2040 – Hinweistext in ETS zu ~20s Blockierung der KNX-Kommunikation während des Datenabrufs
* Feature: Initiale Implementierung (ICS/iCal-Parser, 4 Abfallarten pro Kanal)
