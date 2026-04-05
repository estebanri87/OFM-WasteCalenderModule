<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Steffen Rittmeier -->

# Applikationsbeschreibung Müllkalender (WasteCalendar)

Das Modul liest je Kanal einen Abfallkalender über eine ICS/iCal-URL ein und stellt die nächsten Abholtermine als KNX-Gruppenobjekte bereit.  
Pro Kanal können bis zu 4 Abfallarten (z. B. Restmüll, Biotonne, Papier, Gelbe Tonne) konfiguriert werden, die jeweils über einen Suchbegriff in den Kalendereinträgen identifiziert werden.  
Automatisierungslogik (z. B. Erinnerungsbenachrichtigung am Vorabend) wird nicht im Modul abgebildet – dafür ist das Logikmodul zuständig.

---

# Voraussetzungen

- ICS/iCal-URL des lokalen Abfallentsorgers (öffentlich erreichbar, kein Login erforderlich)
- Aktive Internetverbindung des Geräts
- NTP-Zeitsynchronisation muss aktiv sein

> **Hinweis:** Die meisten kommunalen Abfallentsorger in Deutschland, Österreich und der Schweiz bieten einen ICS-Kalender-Export auf ihrer Website an. Die URL kann dort direkt kopiert werden.

---

# Kanaleinstellungen

<!-- DOC -->
## ICS-URL

URL des iCal-Kalenders des Abfallentsorgers.  
Das Format muss `http://` oder `https://` beginnen.  
Maximale Länge: 80 Zeichen.

Beispiel: `https://www.meinentsorger.de/abfallkalender/2026.ics`

<!-- DOCEND -->

<!-- DOC -->
## Suchbegriff Abfallart 1–4

Schlüsselwort, das im SUMMARY-Feld der iCal-Einträge gesucht wird.  
Groß-/Kleinschreibung wird ignoriert.  
Maximale Länge: 20 Zeichen.

Typische Suchbegriffe:

| Abfallart | Suchbegriff |
|-----------|-------------|
| Restmüll | `Restmüll` oder `Restabfall` |
| Biotonne | `Bio` |
| Papier | `Papier` |
| Gelbe Tonne / Gelber Sack | `Gelb` oder `Verpackung` |

Bleibt das Feld leer, ist die Abfallart deaktiviert.

<!-- DOCEND -->

---

# Aktualisierung

Der Kalender wird einmal täglich automatisch neu abgerufen.  
Der erste Abruf erfolgt 60 Sekunden nach dem Gerätestart (nach NTP-Synchronisation).  
Eine manuelle Aktualisierung ist nicht vorgesehen.

---

# Gruppenobjekte

Pro Kanal und Abfallart stehen 4 Gruppenobjekte zur Verfügung.  
Die Nummern der Abfallart 2–4 folgen in gleichmäßigem Abstand auf Abfallart 1.

| Nr. | Name | DPT | Richtung | Beschreibung |
|-----|------|-----|----------|--------------|
| 0 | Abfallart 1: Tage bis Abholung | 5.010 | Ausgang | Anzahl Tage bis zur nächsten Leerung (0 = heute, 255 = unbekannt) |
| 1 | Abfallart 1: Bezeichnung | 16.001 | Ausgang | Bezeichnung aus dem Kalendereintrag (max. 14 Zeichen) |
| 2 | Abfallart 1: Abholung heute | 1.001 | Ausgang | EIN wenn die Leerung heute stattfindet |
| 3 | Abfallart 1: Abholung morgen | 1.001 | Ausgang | EIN wenn die Leerung morgen stattfindet |
| 4 | Abfallart 2: Tage bis Abholung | 5.010 | Ausgang | wie oben |
| 5 | Abfallart 2: Bezeichnung | 16.001 | Ausgang | wie oben |
| 6 | Abfallart 2: Abholung heute | 1.001 | Ausgang | wie oben |
| 7 | Abfallart 2: Abholung morgen | 1.001 | Ausgang | wie oben |
| 8 | Abfallart 3: Tage bis Abholung | 5.010 | Ausgang | wie oben |
| 9 | Abfallart 3: Bezeichnung | 16.001 | Ausgang | wie oben |
| 10 | Abfallart 3: Abholung heute | 1.001 | Ausgang | wie oben |
| 11 | Abfallart 3: Abholung morgen | 1.001 | Ausgang | wie oben |
| 12 | Abfallart 4: Tage bis Abholung | 5.010 | Ausgang | wie oben |
| 13 | Abfallart 4: Bezeichnung | 16.001 | Ausgang | wie oben |
| 14 | Abfallart 4: Abholung heute | 1.001 | Ausgang | wie oben |
| 15 | Abfallart 4: Abholung morgen | 1.001 | Ausgang | wie oben |

---

# Technische Hinweise

- Unterstützte ICS-Formate: VEVENT mit `DTSTART` (Datum oder Datum+Uhrzeit)
- RRULE (wiederkehrende Einträge) wird **nicht** ausgewertet – die meisten Abfallkalender enthalten ohnehin explizite Einzeltermine
- Maximale Anzahl auswertbarer Einträge pro Abruf: 200
- Maximale Antwortgröße: 32 KB
