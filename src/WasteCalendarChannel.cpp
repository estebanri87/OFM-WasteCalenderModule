#include "WasteCalendarChannel.h"
#include "knxprod.h"

WasteCalendarChannel::WasteCalendarChannel(uint8_t channelIndex)
    : _channelIndex(channelIndex)
{
}

const std::string WasteCalendarChannel::name()
{
    return "WasteCalendarChannel";
}

void WasteCalendarChannel::setup()
{
    logDebugP("WasteCalendar channel %d setup", _channelIndex);
}

void WasteCalendarChannel::loop()
{
    if (!openknx.time.isTimeSynced())
        return;

    uint32_t now = millis();

    if (_firstFetch)
    {
        if (now < WCL_STARTUP_DELAY_MS)
            return;
        _firstFetch = false;
    }
    else
    {
        if ((now - _lastFetch) < WCL_REFRESH_INTERVAL_MS)
            return;
    }

    fetchAndParse();
}

void WasteCalendarChannel::processInputKo(GroupObject& ko)
{
    // keine Eingabe-KOs
}

bool WasteCalendarChannel::fetchAndParse()
{
    const char* url = ParamWCL_CHIcsUrlStr.c_str();
    if (url == nullptr || url[0] == '\0')
    {
        logInfoP("WasteCalendar channel %d: no ICS URL configured", _channelIndex);
        return false;
    }

    logInfoP("WasteCalendar channel %d: fetching %s", _channelIndex, url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);
    int httpCode = http.GET();

    if (httpCode != 200)
    {
        logErrorP("WasteCalendar channel %d: HTTP error %d", _channelIndex, httpCode);
        http.end();
        return false;
    }

    size_t dataLen = http.getSize();
    if (dataLen == 0 || dataLen > WCL_HTTP_BUFFER_SIZE)
    {
        // Fallback: unbekannte Länge, String lesen
        String body = http.getString();
        http.end();
        if (body.length() == 0 || body.length() > WCL_HTTP_BUFFER_SIZE)
        {
            logErrorP("WasteCalendar channel %d: response too large or empty (%d bytes)", _channelIndex, (int)body.length());
            return false;
        }

        WastePickupDate dates[WCL_MAX_EVENTS];
        char summaries[WCL_MAX_EVENTS][64];
        int numEvents = parseIcs(body.c_str(), body.length(), dates, summaries, WCL_MAX_EVENTS);
        logInfoP("WasteCalendar channel %d: parsed %d events", _channelIndex, numEvents);

        evaluateFractions(dates, summaries, numEvents);
    }
    else
    {
        String body = http.getString();
        http.end();

        WastePickupDate dates[WCL_MAX_EVENTS];
        char summaries[WCL_MAX_EVENTS][64];
        int numEvents = parseIcs(body.c_str(), body.length(), dates, summaries, WCL_MAX_EVENTS);
        logInfoP("WasteCalendar channel %d: parsed %d events", _channelIndex, numEvents);

        evaluateFractions(dates, summaries, numEvents);
    }

    publishKos();
    _lastFetch = millis();
    return true;
}

int WasteCalendarChannel::parseIcs(const char* icsData, size_t dataLen,
                                    WastePickupDate* dates, char (*summaries)[64], int maxEvents)
{
    int count = 0;
    bool inEvent = false;

    WastePickupDate currentDate = {0, 0, 0};
    char currentSummary[64] = {0};
    bool hasDate = false;
    bool hasSummary = false;

    const char* p = icsData;
    const char* end = icsData + dataLen;

    while (p < end && count < maxEvents)
    {
        // Zeile extrahieren
        const char* lineStart = p;
        while (p < end && *p != '\n')
            p++;
        size_t lineLen = p - lineStart;
        // \r am Ende entfernen
        if (lineLen > 0 && lineStart[lineLen - 1] == '\r')
            lineLen--;
        if (p < end)
            p++;  // '\n' überspringen

        if (lineLen == 0)
            continue;

        // BEGIN:VEVENT
        if (lineLen == 12 && strncmp(lineStart, "BEGIN:VEVENT", 12) == 0)
        {
            inEvent = true;
            hasDate = false;
            hasSummary = false;
            currentDate = {0, 0, 0};
            currentSummary[0] = '\0';
            continue;
        }

        // END:VEVENT
        if (lineLen == 10 && strncmp(lineStart, "END:VEVENT", 10) == 0)
        {
            if (inEvent && hasDate && hasSummary)
            {
                dates[count] = currentDate;
                strncpy(summaries[count], currentSummary, 63);
                summaries[count][63] = '\0';
                count++;
            }
            inEvent = false;
            continue;
        }

        if (!inEvent)
            continue;

        // DTSTART (verschiedene Formate: DTSTART:20260415, DTSTART;VALUE=DATE:20260415,
        //          DTSTART;TZID=...:20260415T...)
        if (lineLen >= 8 && strncmp(lineStart, "DTSTART", 7) == 0)
        {
            // Doppelpunkt oder Semikolon suchen -> nach letztem ':' direkt das Datum
            const char* colon = (const char*)memchr(lineStart, ':', lineLen);
            if (colon != nullptr && (colon - lineStart + 8) <= (ptrdiff_t)lineLen)
            {
                colon++;  // hinter den ':'
                int y = 0, m = 0, d = 0;
                if (sscanf(colon, "%4d%2d%2d", &y, &m, &d) == 3 && y > 2000)
                {
                    currentDate = {y, m, d};
                    hasDate = true;
                }
            }
            continue;
        }

        // SUMMARY
        if (lineLen >= 8 && strncmp(lineStart, "SUMMARY:", 8) == 0)
        {
            size_t summaryLen = lineLen - 8;
            if (summaryLen > 63) summaryLen = 63;
            strncpy(currentSummary, lineStart + 8, summaryLen);
            currentSummary[summaryLen] = '\0';
            hasSummary = true;
            continue;
        }
    }

    return count;
}

void WasteCalendarChannel::evaluateFractions(const WastePickupDate* dates, const char (*summaries)[64], int numEvents)
{
    // Heutiges Datum ermitteln
    time_t now = ::time(nullptr);
    struct tm* tmToday = localtime(&now);
    int todayYear  = tmToday->tm_year + 1900;
    int todayMonth = tmToday->tm_mon + 1;
    int todayDay   = tmToday->tm_mday;

    // Morgen
    time_t tomorrow_t = now + 86400;
    struct tm* tmTomorrow = localtime(&tomorrow_t);
    int tomorrowYear  = tmTomorrow->tm_year + 1900;
    int tomorrowMonth = tmTomorrow->tm_mon + 1;
    int tomorrowDay   = tmTomorrow->tm_mday;

    for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
    {
        _fractions[f].daysUntilPickup = 255;
        _fractions[f].pickupToday = false;
        _fractions[f].pickupTomorrow = false;
        _fractions[f].name[0] = '\0';

        const char* keyword = nullptr;
        switch (f)
        {
            case 0: keyword = ParamWCL_CHFraction1KeywordStr.c_str(); break;
            case 1: keyword = ParamWCL_CHFraction2KeywordStr.c_str(); break;
            case 2: keyword = ParamWCL_CHFraction3KeywordStr.c_str(); break;
            case 3: keyword = ParamWCL_CHFraction4KeywordStr.c_str(); break;
        }

        if (keyword == nullptr || keyword[0] == '\0')
            continue;

        // Nächsten Termin >= heute suchen
        int bestDays = 999;
        int bestEvent = -1;

        for (int e = 0; e < numEvents; e++)
        {
            // SUMMARY enthält Suchbegriff?
            if (strcasestr(summaries[e], keyword) == nullptr)
                continue;

            const WastePickupDate& d = dates[e];

            // Tage berechnen
            // Einfache Tagesberechnung über mktime
            struct tm tmEvent = {};
            tmEvent.tm_year = d.year - 1900;
            tmEvent.tm_mon  = d.month - 1;
            tmEvent.tm_mday = d.day;
            tmEvent.tm_hour = 12;
            time_t eventTime = mktime(&tmEvent);

            // Tage seit Mitternacht heute
            struct tm tmTodayNoon = {};
            tmTodayNoon.tm_year = todayYear - 1900;
            tmTodayNoon.tm_mon  = todayMonth - 1;
            tmTodayNoon.tm_mday = todayDay;
            tmTodayNoon.tm_hour = 12;
            time_t todayNoon = mktime(&tmTodayNoon);

            int diffDays = (int)((eventTime - todayNoon) / 86400);
            if (diffDays < 0)
                continue;  // vergangene Termine ignorieren

            if (diffDays < bestDays)
            {
                bestDays = diffDays;
                bestEvent = e;
            }
        }

        if (bestEvent >= 0)
        {
            _fractions[f].daysUntilPickup = (uint8_t)(bestDays > 254 ? 254 : bestDays);
            _fractions[f].pickupToday = (bestDays == 0);
            _fractions[f].pickupTomorrow = (bestDays == 1);

            // Name: SUMMARY kürzen auf 14 Zeichen
            strncpy(_fractions[f].name, summaries[bestEvent], 14);
            _fractions[f].name[14] = '\0';
        }
    }
}

void WasteCalendarChannel::publishKos()
{
    for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
    {
        uint8_t koOffset = f * WCL_KoBlockSizeFraction;

        // Tage bis Abholung
        KnxGroupObject* koDays = openknx.getKo(WCL_KoCHFr1DaysUntilPickup + koOffset);
        if (koDays != nullptr)
            koDays->value(_fractions[f].daysUntilPickup, DPT_Value_1_Ucount);

        // Bezeichnung
        KnxGroupObject* koName = openknx.getKo(WCL_KoCHFr1Name + koOffset);
        if (koName != nullptr)
            koName->value(_fractions[f].name, DPT_String_ASCII);

        // Abholung heute
        KnxGroupObject* koToday = openknx.getKo(WCL_KoCHFr1PickupToday + koOffset);
        if (koToday != nullptr)
            koToday->value(_fractions[f].pickupToday, DPT_Switch);

        // Abholung morgen
        KnxGroupObject* koTomorrow = openknx.getKo(WCL_KoCHFr1PickupTomorrow + koOffset);
        if (koTomorrow != nullptr)
            koTomorrow->value(_fractions[f].pickupTomorrow, DPT_Switch);
    }
}
