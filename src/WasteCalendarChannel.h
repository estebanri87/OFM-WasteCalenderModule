#pragma once
#include "OpenKNX.h"
#include "HTTPClient.h"
#include <ArduinoJson.h>

#define WCL_NUM_FRACTIONS 4

// Startup-Verzögerung vor dem ersten Abruf
#define WCL_STARTUP_DELAY_MS (60UL * 1000UL)

// Maximale Anzahl Events im ICS-Puffer
#define WCL_MAX_EVENTS 200

// Länge des ICS-Puffers für HTTP-Response
#define WCL_HTTP_BUFFER_SIZE (32 * 1024)

// KO für "Mülldaten aktualisieren" (Eingang, Auslöser) – definiert in knxprod.h nach Regenerierung
// Fallback: WCL_KoBlockOffset - 1 ist nach Regenerierung mit globalem KO korrekt
#ifndef WCL_KoRefreshData
#define WCL_KoRefreshData (WCL_KoBlockOffset - 1)
#endif

struct WastePickupDate
{
    int year;   // z.B. 2026
    int month;  // 1..12
    int day;    // 1..31
};

struct WasteFractionState
{
    uint8_t daysUntilPickup = 255;  // 255 = unbekannt
    char    name[15] = {0};         // 14 Zeichen + Nullterminator
    bool    pickupToday = false;
    bool    pickupTomorrow = false;
};

class WasteCalendarChannel : public OpenKNX::Channel
{
  protected:
    uint8_t _channelIndex = 0;
    uint32_t _lastFetch = 0;
    int _lastFetchDay = -1; // Tag des letzten Abrufs (tm_yday), -1 = noch nie
    bool _firstFetch = true;

#ifdef ARDUINO_ARCH_ESP32
    enum WclFetchState { WCL_FETCH_IDLE, WCL_FETCH_RUNNING, WCL_FETCH_DONE };
    volatile WclFetchState _fetchState = WCL_FETCH_IDLE;
    TaskHandle_t _fetchTaskHandle = nullptr;
    static void fetchTaskEntry(void* param);
#endif

    WasteFractionState _fractions[WCL_NUM_FRACTIONS];

    // ICS herunterladen und parsen, Ergebnis in _fractions schreiben
    bool __attribute__((noinline)) fetchAndParse();

    // Interne Implementierung mit heap-allokierten Puffern (vermeidet Stack-Overflow auf ESP32)
    bool __attribute__((noinline)) fetchAndParseInternal(WastePickupDate* dates, char (*summaries)[64]);

    // app.abfallplus.de: 11-Schritt-Wizard → Plist-XML streamen und auswerten
    bool __attribute__((noinline)) fetchAndParseAbfallPlus();

    // müll.io: POST mit Adress-Headern → JSON direkt auswerten
    bool __attribute__((noinline)) fetchAndParseMuellIo();

    // Einzelne Zeile aus ICS auswerten
    // events: Array der gefundenen (Datum, Zusammenfassung)-Paare
    // returns Anzahl der gelesenen Events
    int parseIcs(const char* icsData, size_t dataLen,
                 WastePickupDate* dates, char (*summaries)[64], int maxEvents);

    // Für jede Fraktion den nächsten Termin suchen und Zustand berechnen
    void evaluateFractions(const WastePickupDate* dates, const char (*summaries)[64], int numEvents);

    // KOs publizieren
    void publishKos();

  public:
    WasteCalendarChannel(uint8_t channelIndex);

    const std::string name() override;
    void setup() override;
    void loop() override;
    void processInputKo(GroupObject& ko) override;
};
