<!-- SPDX-License-Identifier: AGPL-3.0-only -->
<!-- Copyright (C) 2026 Steffen Rittmeier -->

# Applikationsbeschreibung Müllkalender (WasteCalendar)

Das Modul ruft je Kanal einen Abfallkalender ab und stellt die nächsten Abholtermine als KNX-Gruppenobjekte bereit.  
Pro Kanal können bis zu 4 Abfallarten konfiguriert werden.  
Automatisierungslogik (z. B. Erinnerungsbenachrichtigung am Vorabend) ist Aufgabe des Logikmoduls.

---

## Inhaltsverzeichnis

- [Voraussetzungen](#voraussetzungen)
- [Datenquellen (Modus-Auswahl)](#datenquellen-modus-auswahl)
- [Modus 0: ICS-URL](#modus-0-ics-url)
  - [ICS-URL](#ics-url)
  - [Suchbegriff Abfallart 1–4](#suchbegriff-abfallart-14)
- [Modus 1: Apps by Abfall+](#modus-1-apps-by-abfall)
  - [App-ID](#app-id)
  - [Stadt/Gemeinde](#stadtgemeinde)
  - [Straße](#straße)
  - [Hausnummer](#hausnummer)
  - [Landkreis](#landkreis-opt)
  - [Bezirk](#bezirk-opt)
  - [Bundesland](#bundesland-opt)
  - [Abfallart 1–4 (Dropdown)](#abfallart-14-dropdown)
  - [Unterstützte App-IDs](#unterstützte-app-ids)
- [Modus 2: müll.io](#modus-2-müllio)
  - [Straße, Hausnummer, PLZ, Ort](#straße-hausnummer-plz-ort)
- [Aktualisierung](#aktualisierung)
- [Gruppenobjekte](#gruppenobjekte)

---

# Voraussetzungen

- Aktive Internetverbindung des Geräts
- NTP-Zeitsynchronisation muss aktiv sein

---

# Datenquellen (Modus-Auswahl)

Pro Kanal kann eine von drei Datenquellen gewählt werden:

| Modus | Bezeichnung | Beschreibung |
|-------|-------------|--------------|
| 0 | **ICS-URL** | Beliebige öffentliche iCal/ICS-URL |
| 1 | **Apps by Abfall+** | Abfall+ App (über 100 deutsche Landkreise/Städte) |
| 2 | **müll.io** | müll.io-Dienst |

---

# Modus 0: ICS-URL

<!-- DOC HelpContext="ICS-URL" -->
## ICS-URL

URL des iCal-Kalenders des Abfallentsorgers.  
Das Format muss `http://` oder `https://` beginnen.  
Maximale Länge: 160 Zeichen.

Beispiel: `https://www.meinentsorger.de/abfallkalender/2026.ics`

<!-- DOCEND -->

<!-- DOC HelpContext="Suchbegriff-Abfallart" -->
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

# Modus 1: Apps by Abfall+

Dieser Modus nutzt die [Abfall+ App-Plattform](https://www.abfallplus.de/) direkt über die API unter `app.abfallplus.de`.  
Es werden über 100 Entsorgungsregionen in Deutschland unterstützt (siehe Liste unten).

<!-- DOC HelpContext="App-ID" -->
## App-ID

Die eindeutige App-ID des zuständigen Entsorgers. Entspricht der Android-Paket-ID der jeweiligen App.  
Maximale Länge: 32 Zeichen.

Beispiel: `de.ucom.abfallavr`

Die App-ID findet sich im Play Store: `https://play.google.com/store/apps/details?id=<app_id>`

<!-- DOCEND -->

<!-- DOC HelpContext="Stadt-Gemeinde" -->
## Stadt/Gemeinde

Name der Stadt oder Gemeinde, wie in der Abfall+-App angezeigt.  
Maximale Länge: 32 Zeichen.

Beispiel: `Neckargemünd`

<!-- DOCEND -->

<!-- DOC HelpContext="Strasse" -->
## Straße

Name der Straße, wie in der Abfall+-App angezeigt.  
Maximale Länge: 40 Zeichen.

Beispiel: `Kohlackerweg`

<!-- DOCEND -->

<!-- DOC HelpContext="Hausnummer" -->
## Hausnummer

Hausnummer (optional). Wenn leer, werden alle Hausnummern der Straße berücksichtigt.  
Maximale Länge: 10 Zeichen.

Beispiel: `2`

<!-- DOCEND -->

<!-- DOC HelpContext="Landkreis" -->
## Landkreis (opt.)

Nur bei Apps, die mehrere Landkreise unterstützen (z. B. `de.albagroup.app`, `de.abfallwecker`).  
Teilbegriff genügt dank Substring-Suche (z. B. `Nordsach` für `Landkreis Nordsachsen`).  
Maximale Länge: 32 Zeichen.

Beispiel: `Rhein-Neckar` oder `Tübingen`

<!-- DOCEND -->

<!-- DOC HelpContext="Bezirk" -->
## Bezirk (opt.)

Nur bei Apps mit Bezirksauswahl (z. B. `de.k4systems.leipziglk`, `de.k4systems.lkgoettingen`).  
Maximale Länge: 20 Zeichen.

Beispiel: `Brandis`

<!-- DOCEND -->

<!-- DOC HelpContext="Bundesland" -->
## Bundesland (opt.)

Nur bei Apps, die in mehreren Bundesländern aktiv sind (z. B. `de.abfallwecker`, `de.k4systems.unterallgaeu`).  
Wird automatisch aus der App-Konfiguration erkannt, sofern eindeutig. Nur bei mehrdeutigen Apps manuell angeben.  
Maximale Länge: 20 Zeichen.

Beispiel: `Baden-Württemberg`

<!-- DOCEND -->

<!-- DOC HelpContext="Abfallart-Dropdown" -->
## Abfallart 1–4 (Dropdown)

Auswahl der gewünschten Abfallart aus einer vordefinierten Liste.  
**Hinweis:** Die Dropdown-Optionen (Restmüll, BioEnergieTonne usw.) sind spezifisch für den Rhein-Neckar-Kreis (AVR, `de.ucom.abfallavr`). Für alle anderen App-IDs werden die Abfallarten automatisch ermittelt – die Auswahl hat dann keine Wirkung und alle Termine werden gemeldet.

<!-- DOCEND -->

## Unterstützte App-IDs

| App-ID | Region(en) |
|--------|------------|
| de.albagroup.app | Berlin, Braunschweig, Havelland, Oberhavel, Ostprignitz-Ruppin, Tübingen |
| de.k4systems.abfallinfocw | Kreis Calw |
| de.k4systems.abfallinfoapp | Mechernich und Kommunen |
| de.k4systems.abfallappes | Landkreis Esslingen |
| de.k4systems.egst | Kreis Steinfurt |
| de.idcontor.abfallwbd | Duisburg |
| de.ucom.abfallavr | Rhein-Neckar-Kreis |
| de.k4systems.abfallapprv | Kreis Ravensburg |
| de.k4systems.avlserviceplus | Kreis Ludwigsburg |
| de.k4systems.muellalarm | Schönmackers |
| de.k4systems.abfallapploe | Kreis Lörrach |
| de.k4systems.abfallapp | Kreis Augsburg |
| de.k4systems.abfallappvorue | Kreis Vorpommern-Rügen |
| de.k4systems.abfallappfds | Kreis Freudenstadt |
| de.k4systems.abfallscout | Kreis Bad Kissingen |
| de.k4systems.avea | Leverkusen |
| de.k4systems.neustadtaisch | Kreis Neustadt/Aisch-Bad Windsheim |
| de.k4systems.abfalllkswp | Kreis Südwestpfalz |
| de.k4systems.awbemsland | Kreis Emsland |
| de.k4systems.abfallappclp | Kreis Cloppenburg |
| de.k4systems.abfallappnf | Kreis Nordfriesland |
| de.k4systems.abfallappog | Ortenaukreis |
| de.k4systems.abfallappmol | Kreis Märkisch-Oderland |
| de.k4systems.kufiapp | Landkreis Wunsiedel im Fichtelgebirge |
| de.k4systems.abfalllkbz | Kreis Bautzen |
| de.k4systems.abfallappbb | Landkreis Böblingen |
| de.k4systems.abfallappla | Landshut |
| de.k4systems.abfallappwug | Kreis Weißenburg-Gunzenhausen |
| de.k4systems.abfallappik | Ilm-Kreis |
| de.k4systems.leipziglk | Landkreis Leipzig |
| de.k4systems.abfallappbk | Bad Kissingen |
| de.cmcitymedia.hokwaste | Hohenlohekreis |
| de.abfallwecker | Tuttlingen, Prignitz, Osterode am Harz, Nordsachsen |
| de.k4systems.abfallappka | Kreis Karlsruhe |
| de.k4systems.lkgoettingen | Kreis Göttingen |
| de.k4systems.abfallappcux | Kreis Cuxhaven |
| de.k4systems.abfallslk | Salzlandkreis |
| de.k4systems.abfallappzak | ZAK Kempten |
| de.zawsr | ZAW-SR |
| de.k4systems.teamorange | Kreis Würzburg |
| de.k4systems.abfallappvivo | Kreis Miesbach |
| de.k4systems.lkgr | Landkreis Görlitz |
| de.k4systems.zawdw | AWG Donau-Wald |
| de.k4systems.abfallappgib | Kreis Wesermarsch |
| de.k4systems.wuerzburg | Würzburg |
| de.k4systems.abfallappgap | Kreis Garmisch-Partenkirchen |
| de.k4systems.bonnorange | Bonn |
| de.gimik.apps.muellwecker_neuwied | Kreis Neuwied |
| abfallH.ucom.de | Kreis Heilbronn |
| de.k4systems.abfallappts | Kreis Traunstein |
| de.k4systems.awa | Augsburg |
| de.k4systems.abfallappfuerth | Kreis Fürth |
| de.k4systems.abfallwelt | Kreis Kitzingen |
| de.k4systems.lkemmendingen | Kreis Emmendingen |
| de.k4systems.abfallkreisrt | Kreis Reutlingen |
| de.abfallplus.tbrapp | Reutlingen |
| de.k4systems.abfallappmetz | Metzingen |
| de.k4systems.abfallappmyk | Kreis Mayen-Koblenz |
| de.k4systems.abfallappoal | Kreis Ostallgäu |
| de.k4systems.regioentsorgung | Alsdorf, Baesweiler, Eschweiler, Herzogenrath u. a. |
| de.k4systems.abfalllkbt | Kreis Bayreuth |
| de.k4systems.awvapp | Kreis Vechta |
| de.k4systems.aevapp | Schwarze Elster |
| de.k4systems.awbgp | Kreis Göppingen |
| de.k4systems.abfallhr | ALF Lahn-Fulda |
| de.k4systems.abfallappbh | Kreis Breisgau-Hochschwarzwald |
| de.k4systems.awgbassum | Kreis Diepholz |
| de.data_at_work.aws | Kreis Schaumburg |
| de.k4systems.hebhagen | Hagen |
| de.k4systems.meinawblm | Kreis Limburg-Weilburg |
| de.k4systems.abfallmsp | Landkreis Main-Spessart |
| de.k4systems.asoapp | Kreis Osterholz |
| de.k4systems.awistasta | Kreis Starnberg |
| de.ucom.abfallebe | Essen |
| de.k4systems.bawnapp | Kreis Nienburg / Weser |
| de.k4systems.abfallappol | Oldenburg |
| de.k4systems.awbrastatt | Kreis Rastatt |
| de.k4systems.abfallappmil | Kreis Miltenberg |
| de.k4systems.abfallsbk | Schwarzwald-Baar-Kreis |
| de.k4systems.wabapp | Westerwaldkreis |
| de.k4systems.llabfallapp | Kreis Landsberg am Lech |
| de.k4systems.lkruelzen | Kreis Uelzen |
| de.k4systems.abfallzak | Zollernalbkreis |
| de.k4systems.abfallappno | Neckar-Odenwald-Kreis |
| de.k4systems.udb | Burgenland (Landkreis) |
| de.k4systems.abfallappsig | Kreis Sigmaringen |
| de.k4systems.asf | Freiburg im Breisgau |
| de.drekopf.abfallplaner | Drekopf |
| de.k4systems.unterallgaeu | Rottweil, Tuttlingen, Waldshut, Frankfurt (Oder), Prignitz |
| de.k4systems.landshutlk | Kreis Landshut |
| de.k4systems.zakb | Kreis Bergstraße |
| de.k4systems.awrplus | Kreis Rotenburg (Wümme) |
| de.k4systems.lkmabfallplus | München Landkreis |
| de.k4systems.athosmobil | ATHOS GmbH |
| de.k4systems.willkommen | Rottweil, Tuttlingen, Waldshut, Frankfurt (Oder), Prignitz |
| de.idcontor.abfalllu | Ludwigshafen |
| de.ahrweiler.meinawb | Kreis Ahrweiler |
| de.edg.abfallapp | Entsorgung Dortmund GmbH (EDG) |
| de.biberach.abfallapp | Kreis Biberach |
| de.abfallplus.abfallappver | Kreis Verden |
| de.abfallplus.abfallappwt | Kreis Waldshut |
| de.remondis.rheinland | Remondis Rheinland |
| de.abfallplus.gfaabfallinfo | Kreis Lüneburg |
| de.abfallplus.abfalllkrw | Kreis Rottweil |
| de.cmcitymedia.shawaste | Kreis Schwäbisch-Hall |

## Konfigurationsbeispiele

**Einfach (nur Pflichtfelder, App hat festen Landkreis):**
```yaml
App-ID:        de.ucom.abfallavr
Stadt/Gemeinde: Neckargemünd
Straße:        Kohlackerweg
Hausnummer:    2
```

**Mit Landkreis (Multi-Landkreis-App):**
```yaml
App-ID:         de.abfallwecker
Stadt/Gemeinde: Lauchringen
Straße:         Bundesstr.
Hausnummer:     20
Bundesland:     Baden-Württemberg
Landkreis:      Kreis Waldshut
```

**Mit Bezirk:**
```yaml
App-ID:         de.k4systems.leipziglk
Stadt/Gemeinde: Brandis
Straße:         Hauptstraße
Bezirk:         Brandis
```

---

# Modus 2: müll.io

<!-- DOC HelpContext="Strasse-Hausnummer-PLZ-Ort" -->
## Straße, Hausnummer, PLZ, Ort

Adresse für die Abfallabholung über den müll.io-Dienst.

<!-- DOCEND -->

---

# Aktualisierung

Der Kalender wird einmal täglich automatisch neu abgerufen.  
Der erste Abruf erfolgt 60 Sekunden nach dem Gerätestart (nach NTP-Synchronisation).  
Eine manuelle Aktualisierung kann über das KO „Mülldaten aktualisieren" ausgelöst werden.

---

# Gruppenobjekte

Pro Kanal und Abfallart stehen 4 Gruppenobjekte zur Verfügung.

| Nr. | Name | DPT | Richtung | Beschreibung |
|-----|------|-----|----------|--------------|
| 0 | Abfallart 1: Tage bis Abholung | 5.010 | Ausgang | Anzahl Tage bis zur nächsten Leerung (0 = heute, 255 = unbekannt) |
| 1 | Abfallart 1: Bezeichnung | 16.001 | Ausgang | Bezeichnung der Abfallart (max. 14 Zeichen) |
| 2 | Abfallart 1: Abholung heute | 1.001 | Ausgang | EIN wenn die Leerung heute stattfindet |
| 3 | Abfallart 1: Abholung morgen | 1.001 | Ausgang | EIN wenn die Leerung morgen stattfindet |
| 4–7 | Abfallart 2 | wie oben | | |
| 8–11 | Abfallart 3 | wie oben | | |
| 12–15 | Abfallart 4 | wie oben | | |

---

# Technische Hinweise

- Modus 0 (ICS): RRULE (wiederkehrende Einträge) wird nicht ausgewertet – die meisten Abfallkalender enthalten explizite Einzeltermine
- Modus 0 (ICS): Maximale Anzahl auswertbarer Einträge: 200; maximale Antwortgröße: 32 KB
- Modus 1 (Apps by Abfall+): Die API erfordert mehrere sequenzielle HTTP-Anfragen mit je ~1 Sekunde Pause (Wizard-Ablauf). Ein vollständiger Abruf dauert ca. 15–25 Sekunden
- Modus 1 (Apps by Abfall+): Landkreis- und Bezirk-Felder nutzen Substring-Suche (Groß-/Kleinschreibung egal) – ein Teilbegriff genügt
- Modus 1 (Apps by Abfall+): Die Abfallarten-Dropdown-Optionen sind nur für `de.ucom.abfallavr` (Rhein-Neckar-Kreis) vordefiniert; für alle anderen App-IDs werden alle verfügbaren Termine gemeldet
