#pragma once
#include "OpenKNX.h"
#include "HTTPClient.h"

#define WCL_NUM_FRACTIONS 4

// Refresh interval: alle 24 Stunden (in Millisekunden)
#define WCL_REFRESH_INTERVAL_MS (24UL * 60UL * 60UL * 1000UL)

// Startup-Verzögerung vor dem ersten Abruf
#define WCL_STARTUP_DELAY_MS (60UL * 1000UL)

// Maximale Anzahl Events im ICS-Puffer
#define WCL_MAX_EVENTS 200

// Länge des ICS-Puffers für HTTP-Response
#define WCL_HTTP_BUFFER_SIZE (32 * 1024)

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
    bool _firstFetch = true;

    WasteFractionState _fractions[WCL_NUM_FRACTIONS];

    // ICS herunterladen und parsen, Ergebnis in _fractions schreiben
    bool fetchAndParse();

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
