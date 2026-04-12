#include "WasteCalendarChannel.h"
#include "knxprod.h"
#ifdef ARDUINO_ARCH_ESP32
#include <esp_task_wdt.h>
#include <miniz.h>
#endif

// Forward declarations
static std::string latin1ToUtf8(const std::string& s);
static void utf8ToLatin1(const char* src, char* dst, size_t dstSize);

WasteCalendarChannel::WasteCalendarChannel(uint8_t channelIndex)
    : _channelIndex(channelIndex)
{
}

#ifdef ARDUINO_ARCH_ESP32
void WasteCalendarChannel::fetchTaskEntry(void* param)
{
    esp_task_wdt_add(nullptr);
    WasteCalendarChannel* self = static_cast<WasteCalendarChannel*>(param);
    self->fetchAndParse();
    self->_fetchState = WCL_FETCH_DONE;
    self->_fetchTaskHandle = nullptr;
    esp_task_wdt_delete(nullptr);
    vTaskDelete(nullptr);
}
#endif

const std::string WasteCalendarChannel::name()
{
    return "WasteCalendar";
}

void WasteCalendarChannel::setup()
{
    logDebugP("WasteCalendar channel %d setup", _channelIndex);
}

void WasteCalendarChannel::loop()
{
    time_t now = time(nullptr);
    if (now < 1577836800LL)
        return;

#ifdef ARDUINO_ARCH_ESP32
    if (_fetchState == WCL_FETCH_DONE)
    {
        _fetchState = WCL_FETCH_IDLE;
        _lastFetchDay = localtime(&now)->tm_yday;
        return;
    }
    if (_fetchState == WCL_FETCH_RUNNING)
        return;
#endif

    if (_firstFetch)
    {
        if (millis() < WCL_STARTUP_DELAY_MS)
            return;
        _firstFetch = false;
    }
    else
    {
        // Täglicher Abruf ab konfigurierter Stunde, maximal einmal pro Kalendertag
        uint8_t fetchHour = ParamWCL_CHFetchHour;
        struct tm* tm = localtime(&now);
        if (tm->tm_hour < fetchHour)
            return;
        if (tm->tm_yday == _lastFetchDay)
            return; // heute bereits abgerufen
    }

#ifdef ARDUINO_ARCH_ESP32
    _fetchState = WCL_FETCH_RUNNING;
    if (xTaskCreatePinnedToCore(fetchTaskEntry, "WCL_fetch", 16384, this, 1, &_fetchTaskHandle, 0) != pdPASS)
    {
        logErrorP("WCL ch%d: Task-Erstellung fehlgeschlagen, führe synchron aus", _channelIndex);
        _fetchState = WCL_FETCH_IDLE;
        fetchAndParse();
        _lastFetchDay = localtime(&now)->tm_yday;
    }
#else
    fetchAndParse();
    _lastFetchDay = localtime(&now)->tm_yday;
#endif
}

void WasteCalendarChannel::processInputKo(GroupObject& ko)
{
    if (ko.asap() == WCL_KoRefreshData)
    {
        if (time(nullptr) >= 1577836800LL)
        {
            _lastFetchDay = -1; // Erneuten Abruf erzwingen
            _firstFetch = false;
        }
    }
}

bool WasteCalendarChannel::fetchAndParse()
{
    uint8_t mode = ParamWCL_CHMode;

    if (mode == 2)
        return fetchAndParseMuellIo();

    if (mode == 1)
        return fetchAndParseAbfallPlus();

    // Heap-Allokation: dates[200]*12B + summaries[200][64] = ~15KB → zu groß für ESP32-Stack (8KB)
    WastePickupDate* dates = new WastePickupDate[WCL_MAX_EVENTS];
    char (*summaries)[64] = new char[WCL_MAX_EVENTS][64];

    bool result = fetchAndParseInternal(dates, summaries);

    delete[] dates;
    delete[] summaries;
    return result;
}

bool WasteCalendarChannel::fetchAndParseInternal(WastePickupDate* dates, char (*summaries)[64])
{
    std::string urlStr = ParamWCL_CHIcsUrlStr;
    if (urlStr.empty())
    {
        logDebugP("no ICS URL configured");
        _lastFetch = millis();
        return false;
    }

    logDebugP("fetching ICS");

    // HTTPClient auf Heap: das Objekt ist ~300 Bytes groß und würde den ESP32-Stack-Frame sprengen
    HTTPClient* http = new HTTPClient();
#ifdef ARDUINO_ARCH_RP2040
    http->setInsecure();
#endif
    http->begin(urlStr.c_str());
    http->setTimeout(15000);
    int httpCode = http->GET();

    if (httpCode != 200)
    {
        logInfoP("HTTP error %d", httpCode);
        http->end();
        delete http;
        _lastFetch = millis();
        return false;
    }

    String body = http->getString();
    http->end();
    delete http;

    if (body.length() == 0 || body.length() > WCL_HTTP_BUFFER_SIZE)
    {
        logInfoP("response empty or too large (%d bytes)", (int)body.length());
        return false;
    }

    int numEvents = parseIcs(body.c_str(), body.length(), dates, summaries, WCL_MAX_EVENTS);
    logDebugP("parsed %d events", numEvents);

    evaluateFractions(dates, summaries, numEvents);
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
            // ICS ist UTF-8, ETS-Parameter sind ISO-8859-1 → SUMMARY zu ISO-8859-1 konvertieren
            const char* src = lineStart + 8;
            const char* srcEnd = lineStart + lineLen;
            size_t outPos = 0;
            while (src < srcEnd && outPos < 63)
            {
                uint8_t c = (uint8_t)*src;
                if (c < 0x80)
                {
                    // ASCII: direkt übernehmen
                    currentSummary[outPos++] = (char)c;
                    src++;
                }
                else if (c == 0xC2 && (src + 1) < srcEnd)
                {
                    // U+0080..U+00BF: ISO-8859-1 = zweites Byte
                    uint8_t c2 = (uint8_t)*(src + 1);
                    if (c2 >= 0x80 && c2 <= 0xBF)
                        currentSummary[outPos++] = (char)c2;
                    src += 2;
                }
                else if (c == 0xC3 && (src + 1) < srcEnd)
                {
                    // U+00C0..U+00FF: ISO-8859-1 = zweites Byte + 0x40
                    uint8_t c2 = (uint8_t)*(src + 1);
                    if (c2 >= 0x80 && c2 <= 0xBF)
                        currentSummary[outPos++] = (char)(c2 + 0x40);
                    src += 2;
                }
                else
                {
                    // Sonstige Multi-Byte-Sequenz: Byte überspringen
                    src++;
                }
            }
            currentSummary[outPos] = '\0';
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

    for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
    {
        _fractions[f].daysUntilPickup = 255;
        _fractions[f].pickupToday = false;
        _fractions[f].pickupTomorrow = false;
        _fractions[f].name[0] = '\0';

        std::string keywordStr;
        switch (f)
        {
            case 0: keywordStr = ParamWCL_CHFr1KeywordStr; break;
            case 1: keywordStr = ParamWCL_CHFr2KeywordStr; break;
            case 2: keywordStr = ParamWCL_CHFr3KeywordStr; break;
            case 3: keywordStr = ParamWCL_CHFr4KeywordStr; break;
        }
        const char* keyword = keywordStr.c_str();

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
            utf8ToLatin1(summaries[bestEvent], _fractions[f].name, 15);
        }
    }
}

void WasteCalendarChannel::publishKos()
{
    // KOs je Fraktion: DaysUntilPickup, Name, PickupToday, PickupTomorrow = 4 KOs
    static const uint8_t KO_PER_FRACTION = 4;

    for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
    {
        uint8_t koOffset = f * KO_PER_FRACTION;

        // Tage bis Abholung
        knx.getGroupObject(WCL_KoCalcNumber(WCL_KoCHFr1DaysUntilPickup + koOffset)).value(_fractions[f].daysUntilPickup, DPT_Value_1_Ucount);

        // Bezeichnung
        knx.getGroupObject(WCL_KoCalcNumber(WCL_KoCHFr1Name + koOffset)).value(_fractions[f].name, DPT_String_ASCII);

        // Abholung heute
        knx.getGroupObject(WCL_KoCalcNumber(WCL_KoCHFr1PickupToday + koOffset)).value(_fractions[f].pickupToday, DPT_Switch);

        // Abholung morgen
        knx.getGroupObject(WCL_KoCalcNumber(WCL_KoCHFr1PickupTomorrow + koOffset)).value(_fractions[f].pickupTomorrow, DPT_Switch);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Latin-1/UTF-8 conversion helpers (implementations further below)

// Hilfsfunktionen für fetchAndParseAbfallPlus
// ─────────────────────────────────────────────────────────────────────────────

// Pseudo-UUID erzeugen (kein kryptografischer Zufallsgenerator nötig)
static void generateUuid(char* buf, uint32_t seed) {
    uint32_t r0 = seed ^ 0xDEAD1234u;
    uint32_t r1 = r0 * 1664525u + 1013904223u;
    uint32_t r2 = r1 * 1664525u + 1013904223u;
    uint32_t r3 = r2 * 1664525u + 1013904223u;
    snprintf(buf, 37, "%08lx-%04lx-4%03lx-%04lx-%08lx%04lx",
        (unsigned long)r0, (unsigned long)((r1 >> 16) & 0xFFFF),
        (unsigned long)(r2 & 0x0FFF), (unsigned long)(0x8000u | ((r3 >> 16) & 0x3FFFu)),
        (unsigned long)(r2 ^ r3), (unsigned long)((r0 ^ r1) & 0xFFFF));
}

// URL-Encoding für POST-Body-Werte
static std::string urlEncode(const char* str) {
    std::string result;
    for (const unsigned char* p = (const unsigned char*)str; *p; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~') {
            result += (char)*p;
        } else {
            char hex[4];
            snprintf(hex, sizeof(hex), "%%%02X", (unsigned int)*p);
            result += hex;
        }
    }
    return result;
}

// HTTPClient anlegen mit passenden Headern
// azMoz=false: Android-UA (für config.xml, login/, version.xml, struktur.xml.zip)
// azMoz=true: Mozilla-UA mit Browser-Headern (für assistent/*-Schritte)
static HTTPClient* makeAbfallHttp(const char* url, const std::string& cookie, bool mozUa) {
    HTTPClient* http = new HTTPClient();
#ifdef ARDUINO_ARCH_RP2040
    http->setInsecure();
#endif
    http->begin(url);
    http->setTimeout(15000);
    static const char* hdrs[] = { "Location", "Content-Type", "Transfer-Encoding", "Content-Encoding" };
    http->collectHeaders(hdrs, 4);
    if (mozUa) {
        http->addHeader("User-Agent", "Mozilla/5.0 (Linux; Android 10; K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.0.0 Safari/537.36 Abfallwecker");
        http->addHeader("Accept", "*/*");
        http->addHeader("Accept-Encoding", "identity");
        http->addHeader("Origin", "https://app.abfallplus.de");
        http->addHeader("X-Requested-With", "XMLHttpRequest");
        http->addHeader("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
        http->addHeader("Referer", "https://app.abfallplus.de/login/");
        http->addHeader("Accept-Language", "de-DE,de;q=0.9");
    } else {
        http->addHeader("User-Agent", "Android / AVR Abfall 8.1.1 (1915081010)");
        http->addHeader("Content-Type", "application/x-www-form-urlencoded");
    }
    if (!cookie.empty())
        http->addHeader("Cookie", cookie.c_str());
    return http;
}

// Stream zeilenweise lesen und awk_standort_auswahl_step_fertig-Eintrag suchen
// arg2 muss genau searchText entsprechen; zurückgegeben wird der Teil von arg1 vor dem ersten '|'
static std::string streamExtractAwkId(HTTPClient* http, const char* searchText) {
    Stream* s = &http->getStream();
    char buf[600];
    std::string result;
    unsigned long deadline = millis() + 10000;
    const char* FUNC = "awk_standort_auswahl_step_fertig('";
    const size_t FUNC_LEN = strlen(FUNC);

    while (millis() < deadline) {
        if (!s->available()) {
            if (!http->connected()) break;
            delay(1);
            continue;
        }
        int n = s->readBytesUntil('\n', buf, (int)sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';

        const char* p = buf;
        while ((p = strstr(p, FUNC)) != nullptr) {
            p += FUNC_LEN;
            const char* a1End = strchr(p, '\'');
            if (!a1End) { p++; continue; }
            const char* a2p = a1End + 1;
            if (*a2p != ',') { p = a1End + 1; continue; }
            a2p++;
            if (*a2p != '\'') { p = a1End + 1; continue; }
            a2p++;
            const char* a2End = strchr(a2p, '\'');
            if (!a2End) { p = a1End + 1; continue; }
            int a2Len = a2End - a2p;
            {
                size_t sLen = strlen(searchText);
                bool matched = false;
                if (sLen >= 3 && a2Len >= (int)sLen) {
                    for (int si = 0; si <= a2Len - (int)sLen && !matched; si++)
                        if (strncasecmp(a2p + si, searchText, sLen) == 0) matched = true;
                } else {
                    matched = (a2Len == (int)sLen && strncasecmp(a2p, searchText, sLen) == 0);
                }
                if (matched) {
                    const char* pipe = (const char*)memchr(p, '|', a1End - p);
                    result.assign(p, pipe ? pipe : a1End);
                    goto done;
                }
            }
            p = a1End + 1;
        }
    }
done:
    return result;
}

// Stream zeilenweise lesen und Wert eines hidden-input-Feldes extrahieren
// Sucht name="{fieldName}" in einer Zeile und extrahiert value="..." bis stopChar
static std::string streamExtractHidden(HTTPClient* http, const char* fieldName, char stopChar) {
    Stream* s = &http->getStream();
    char buf[600];
    std::string result;
    std::string searchPat = std::string("name=\"") + fieldName + "\"";
    unsigned long deadline = millis() + 12000;

    while (millis() < deadline) {
        if (!s->available()) {
            if (!http->connected()) break;
            delay(1);
            continue;
        }
        int n = s->readBytesUntil('\n', buf, (int)sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        if (strstr(buf, searchPat.c_str()) != nullptr) {
            const char* val = strstr(buf, "value=\"");
            if (val) {
                val += 7;
                const char* end = strchr(val, stopChar);
                if (end) { result.assign(val, end); break; }
                const char* end2 = strchr(val, '"');
                if (end2) { result.assign(val, end2); break; }
            }
        }
    }
    return result;
}

// Login-HTML durchsuchen und Bundesland- und Landkreis-ID aus hidden inputs extrahieren
static void streamExtractLoginFields(HTTPClient* http,
    std::string& bundeslandId, std::string& landkreisId)
{
    Stream* s = &http->getStream();
    char buf[600];
    unsigned long deadline = millis() + 12000;
    const char* targets[2]  = { "name=\"f_id_bundesland\"", "name=\"f_id_landkreis\"" };
    std::string* outputs[2] = { &bundeslandId, &landkreisId };
    while (millis() < deadline) {
        if (!s->available()) {
            if (!http->connected()) break;
            delay(1);
            continue;
        }
        int n = s->readBytesUntil('\n', buf, (int)sizeof(buf) - 1);
        if (n <= 0) continue;
        buf[n] = '\0';
        for (int i = 0; i < 2; i++) {
            if (!outputs[i]->empty()) continue;
            if (!strstr(buf, targets[i])) continue;
            const char* val = strstr(buf, "value=\"");
            if (!val) continue;
            val += 7;
            const char* end = val;
            while (*end && *end != '"' && *end != '|') end++;
            outputs[i]->assign(val, end);
        }
        if (!bundeslandId.empty() && !landkreisId.empty()) break;
    }
}

// assistent/abfallarten/-Antwort parsen: alle f_id_abfallart[]-Checkboxen → Keyword-Matching
// Liest den gesamten Response-Body (funktioniert auch mit minifiziertem HTML/JSON)
static void streamExtractWasteTypesByKeywords(HTTPClient* http,
    const char* keywords[WCL_NUM_FRACTIONS],
    uint16_t matchedIds[WCL_NUM_FRACTIONS],
    char matchedNames[WCL_NUM_FRACTIONS][32])
{
    for (int f = 0; f < WCL_NUM_FRACTIONS; f++) { matchedIds[f] = 0; matchedNames[f][0] = '\0'; }

    // Stream direkt lesen - setTimeout + readBytes blockiert bis EOF (kein available()-Problem bei TLS)
    String body;
    body.reserve(8192);
    {
        Stream* s = &http->getStream();
        s->setTimeout(8000);
        char buf[512];
        int n;
        while ((n = s->readBytes(buf, sizeof(buf))) > 0) {
            body.concat(buf, n);
            delay(0);
        }
    }
    Serial.printf("[WCL] abfallarten body len=%d heap=%d\n", (int)body.length(), (int)ESP.getFreeHeap());
    // Debug: erste 600 Zeichen des Body loggen
    {
        const char* s0 = body.c_str();
        int plen = (int)strlen(s0);
        if (plen > 600) plen = 600;
        char preview[601];
        memcpy(preview, s0, plen);
        preview[plen] = '\0';
        Serial.printf("[WCL] body preview: %s\n", preview);
    }
    const char* s = body.c_str();

    while (true) {
        // Nächstes f_id_abfallart suchen
        const char* inp = strstr(s, "f_id_abfallart");
        if (!inp) break;
        s = inp + 14;

        // value="NNN" innerhalb von 300 Zeichen suchen
        const char* vp = strstr(s, "value=\"");
        if (!vp || (vp - s) > 300) continue;
        vp += 7;
        uint16_t id = 0;
        const char* vd = vp;
        while (*vd >= '0' && *vd <= '9') { id = (uint16_t)(id * 10u + (uint16_t)(*vd - '0')); vd++; }
        if (*vd != '"' || id == 0) continue;
        s = vd;

        // Schließendes '>' des Input-Tags suchen
        const char* gt = strchr(vd, '>');
        if (!gt || (gt - vd) > 300) continue;
        gt++;

        // Nach dem Input suchen wir den Namen im nachfolgenden Element.
        // HTML-Struktur: <input ... value="810" /> <ion-item ...> ... <p>Name</p> ...
        // Strategie: suche das nächste <p>, <span>, <label> oder direkt Text (kein '<') 
        //            innerhalb von 800 Zeichen nach dem Input-Ende
        const char* textStart = nullptr;
        const char* textEnd   = nullptr;

        // Candidate-Tags der Reihe nach probieren
        const char* tags[] = { "<ion-text", "<p>", "<p ", "<span>", "<span ", "<label>", "<label " };
        const char* best = nullptr;
        for (int t = 0; t < 7; t++) {
            const char* p = strstr(gt, tags[t]);
            if (p && (p - gt) < 800 && (!best || p < best)) best = p;
        }
        if (best) {
            // Öffnendes Tag überspringen → nach '>' suchen
            const char* tagEnd = strchr(best, '>');
            if (tagEnd && (tagEnd - best) < 200) {
                textStart = tagEnd + 1;
                textEnd   = strchr(textStart, '<');
            }
        }
        if (!textStart || !textEnd || textEnd <= textStart) continue;

        int len = (int)(textEnd - textStart);
        // Führende/nachfolgende Leerzeichen entfernen
        while (len > 0 && (textStart[0] == ' ' || textStart[0] == '\t')) { textStart++; len--; }
        while (len > 0 && (textStart[len-1] == ' ' || textStart[len-1] == '\n' || textStart[len-1] == '\r' || textStart[len-1] == '\t')) len--;
        if (len <= 0 || len > 63) continue;

        char typeName[64];
        memcpy(typeName, textStart, len);
        typeName[len] = '\0';

        Serial.printf("[WCL] Abfallart id=%d '%s'\n", id, typeName);

        // Gegen alle konfigurierten Keywords prüfen (case-insensitiv, Teilstring)
        for (int f = 0; f < WCL_NUM_FRACTIONS; f++) {
            if (matchedIds[f]) continue;
            if (!keywords[f] || keywords[f][0] == '\0') continue;
            if (strstr(typeName, keywords[f]) || strstr(keywords[f], typeName)) {
                matchedIds[f] = id;
                strncpy(matchedNames[f], typeName, 31);
                matchedNames[f][31] = '\0';
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// app.abfallplus.de: 11-Schritt-Wizard → Plist-XML streamen und auswerten
// ─────────────────────────────────────────────────────────────────────────────
bool WasteCalendarChannel::fetchAndParseAbfallPlus() {
    // ETS-Parameter lesen
    std::string appId           = ParamWCL_CHAvrKeyStr;
    std::string city            = latin1ToUtf8(ParamWCL_CHAvrModusStr);      // Stadt/Gemeinde
    std::string street          = latin1ToUtf8(ParamWCL_CHAvrTypesStr);      // Straße
    std::string hnr             = ParamWCL_CHAvrHnrStr;                      // Hausnummer
    std::string landkreisParam  = latin1ToUtf8(ParamWCL_CHAvrKommuneStr);    // Landkreis (optional)
    std::string bezirkParam     = latin1ToUtf8(ParamWCL_CHAvrStrasseStr);    // Bezirk (optional)
    std::string bundeslandParam = latin1ToUtf8(ParamWCL_CHAvrBundeslandStr); // Bundesland (optional)

    if (appId.empty() || city.empty() || street.empty()) {
        logInfoP("abfallplus ch%d: App-ID, Stadt oder Straße fehlt", _channelIndex);
        _lastFetch = millis();
        return false;
    }

    // Suchbegriffe für Abfallarten aus ETS-Parametern lesen (Fr1-4Keyword, selbe Bytes wie Modus 0)
    // ETS speichert Strings als Latin-1 → UTF-8 Konvertierung für Umlaute nötig (HTML-Response ist UTF-8)
    std::string frKeywordStrs[WCL_NUM_FRACTIONS] = {
        latin1ToUtf8(ParamWCL_CHFr1KeywordStr),
        latin1ToUtf8(ParamWCL_CHFr2KeywordStr),
        latin1ToUtf8(ParamWCL_CHFr3KeywordStr),
        latin1ToUtf8(ParamWCL_CHFr4KeywordStr)
    };
    // Führende/nachfolgende Leerzeichen aus Keywords entfernen
    for (int f = 0; f < WCL_NUM_FRACTIONS; f++) {
        auto& s = frKeywordStrs[f];
        s.erase(0, s.find_first_not_of(" \t"));
        s.erase(s.find_last_not_of(" \t") + 1);
    }
    // Debug: Keywords ausgeben
    for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
        Serial.printf("[WCL] Fr%d keyword: '%s' (len=%d)\n", f+1, frKeywordStrs[f].c_str(), (int)frKeywordStrs[f].size());
    const char* frKeywords[WCL_NUM_FRACTIONS] = {
        frKeywordStrs[0].c_str(),
        frKeywordStrs[1].c_str(),
        frKeywordStrs[2].c_str(),
        frKeywordStrs[3].c_str()
    };

    // Mindestens einen Suchbegriff eingetragen?
    bool anyFraction = false;
    for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
        if (frKeywords[f] && frKeywords[f][0] != '\0') { anyFraction = true; break; }
    if (!anyFraction) {
        logInfoP("abfallplus ch%d: Keine Abfallart-Suchbegriffe konfiguriert", _channelIndex);
        _lastFetch = millis();
        return false;
    }

    uint16_t frMatchedIds[WCL_NUM_FRACTIONS]   = {};
    char     frMatchedNames[WCL_NUM_FRACTIONS][32] = {};

    // Client-UUID erzeugen
    char clientId[37];
    generateUuid(clientId, (uint32_t)millis() ^ ((uint32_t)_channelIndex << 24) ^ (uint32_t)time(nullptr));
    logInfoP("abfallplus ch%d: UUID=%s", _channelIndex, clientId);

    std::string sessionCookie;

    // ─── Schritt 1: config.xml → Session-Cookie holen ─────────────────────
    {
        HTTPClient* http = new HTTPClient();
#ifdef ARDUINO_ARCH_RP2040
        http->setInsecure();
#endif
        http->begin("https://app.abfallplus.de/config.xml");
        http->setTimeout(15000);
        http->addHeader("User-Agent", "Android / AVR Abfall 8.1.1 (1915081010)");
        http->addHeader("Accept-Encoding", "gzip, deflate, br");
        http->addHeader("Content-Type", "application/x-www-form-urlencoded");
        const char* collectH[] = {"Set-Cookie"};
        http->collectHeaders(collectH, 1);
        String postData = String("client=") + clientId + "&app_id=" + appId.c_str();
        http->POST(postData);
        String setCookieHdr = http->header("Set-Cookie");
        // Extrahiere "name=value" (vor dem ersten Semikolon)
        const char* sc = setCookieHdr.c_str();
        const char* semi = strchr(sc, ';');
        if (semi) sessionCookie.assign(sc, semi);
        else sessionCookie = sc;
        http->end(); delete http;
        logInfoP("abfallplus ch%d: config.xml cookie len=%d", _channelIndex, (int)sessionCookie.size());
    }
    if (sessionCookie.empty()) {
        logInfoP("abfallplus ch%d: Kein Session-Cookie erhalten", _channelIndex);
        _lastFetch = millis();
        return false;
    }
    delay(1100);

    // ─── Schritt 2: login/ → Bundesland- und Landkreis-ID extrahieren ─────
    std::string landkreisId;
    std::string bundeslandId;
    {
        std::string postData = std::string("client=") + clientId + "&app_id=" + appId;
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/login/", sessionCookie, false);
        http->POST(String(postData.c_str()));
        streamExtractLoginFields(http, bundeslandId, landkreisId);
        http->end(); delete http;
        logInfoP("abfallplus ch%d: bundesland=%s landkreis=%s", _channelIndex, bundeslandId.c_str(), landkreisId.c_str());
    }
    // Bundesland-Fallback aus ETS-Parameter wenn Login keinen festen Wert liefert
    if (bundeslandId.empty() && !bundeslandParam.empty())
        bundeslandId = bundeslandParam;
    if (landkreisId.empty() && landkreisParam.empty()) {
        logInfoP("abfallplus ch%d: Landkreis-ID nicht gefunden und kein Landkreis-Parameter gesetzt", _channelIndex);
        _lastFetch = millis();
        return false;
    }
    delay(1100);

    // ─── Schritt 2b: assistent/landkreis/ → Landkreis-ID suchen (falls nötig) ─
    if (landkreisId.empty() && !landkreisParam.empty()) {
        std::string postData = std::string("id_bundesland=") + urlEncode(bundeslandId.c_str());
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/landkreis/", sessionCookie, true);
        http->POST(String(postData.c_str()));
        landkreisId = streamExtractAwkId(http, landkreisParam.c_str());
        http->end(); delete http;
        logInfoP("abfallplus ch%d: landkreis via wizard=%s", _channelIndex, landkreisId.c_str());
        delay(1100);
        if (landkreisId.empty()) {
            logInfoP("abfallplus ch%d: Landkreis '%s' nicht gefunden", _channelIndex, landkreisParam.c_str());
            _lastFetch = millis();
            return false;
        }
    }

    // ─── Schritt 3: assistent/kommune/ → Gemeinde-ID suchen ───────────────
    std::string communeId;
    {
        std::string postData = std::string("id_bundesland=") + urlEncode(bundeslandId.c_str()) +
            "&id_landkreis=" + landkreisId;
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/kommune/", sessionCookie, true);
        http->POST(String(postData.c_str()));
        communeId = streamExtractAwkId(http, city.c_str());
        http->end(); delete http;
        logInfoP("abfallplus ch%d: commune=%s", _channelIndex, communeId.c_str());
    }
    if (communeId.empty()) {
        logInfoP("abfallplus ch%d: Gemeinde '%s' nicht gefunden", _channelIndex, city.c_str());
        _lastFetch = millis();
        return false;
    }
    delay(1100);

    // ─── Schritt 3b: assistent/bezirk/ → Bezirk-ID suchen (falls konfiguriert) ─
    std::string bezirkId;
    if (!bezirkParam.empty()) {
        std::string postData = std::string("id_bundesland=") + urlEncode(bundeslandId.c_str()) +
            "&id_landkreis=" + landkreisId + "&id_kommune=" + communeId;
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/bezirk/", sessionCookie, true);
        http->POST(String(postData.c_str()));
        bezirkId = streamExtractAwkId(http, bezirkParam.c_str());
        http->end(); delete http;
        logInfoP("abfallplus ch%d: bezirk=%s", _channelIndex, bezirkId.c_str());
        delay(1100);
    }

    // ─── Schritt 4: assistent/strasse/ → Straßen-ID suchen ───────────────
    std::string strasseId;
    {
        std::string postData = std::string("id_landkreis=") + landkreisId +
            "&id_bezirk=" + bezirkId + "&id_kommune=" + communeId + "&id_kommune_qry=" + communeId +
            "&strasse_qry=" + urlEncode(street.c_str());
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/strasse/", sessionCookie, true);
        http->POST(String(postData.c_str()));
        strasseId = streamExtractAwkId(http, street.c_str());
        http->end(); delete http;
        logInfoP("abfallplus ch%d: strasse=%s", _channelIndex, strasseId.c_str());
    }
    if (strasseId.empty()) {
        logInfoP("abfallplus ch%d: Straße '%s' nicht gefunden", _channelIndex, street.c_str());
        _lastFetch = millis();
        return false;
    }
    delay(1100);

    // ─── Schritt 5: assistent/hnr/ → Standort-Straßen-ID suchen (optional) ─
    std::string locationStrasseId = strasseId;  // Fallback: Basis-Straßen-ID ohne HNR
    {
        std::string postData = std::string("id_landkreis=") + landkreisId +
            "&id_bezirk=" + bezirkId + "&id_kommune=" + communeId + "&id_strasse=" + strasseId;
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/hnr/", sessionCookie, true);
        http->POST(String(postData.c_str()));
        if (!hnr.empty()) {
            std::string locId = streamExtractAwkId(http, hnr.c_str());
            if (!locId.empty()) locationStrasseId = locId;
            else logInfoP("abfallplus ch%d: HNR '%s' nicht gefunden, verwende Basisstraße", _channelIndex, hnr.c_str());
        }
        http->end(); delete http;
        logInfoP("abfallplus ch%d: locationStrasse=%s", _channelIndex, locationStrasseId.c_str());
    }
    delay(1100);

    // ─── Schritt 6: assistent/abfallarten/ → Abfallart-IDs per Keyword ermitteln ──
    // f_id_strasse: immer die numerische Straßen-ID (strasseId), NICHT locationStrasseId
    // locationStrasseId kann "2/2" sein wenn der HNR-Step die Hausnummer als ID zurückgibt
    auto isNumericId = [](const std::string& s) {
        return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos;
    };
    const std::string& effectiveStrasseId = isNumericId(locationStrasseId) ? locationStrasseId : strasseId;
    std::string baseData = std::string("f_id_bundesland=") + urlEncode(bundeslandId.c_str()) +
        "&f_id_landkreis=" + landkreisId +
        "&f_id_kommune=" + communeId + "&f_id_bezirk=" + bezirkId + "&f_id_strasse=" + effectiveStrasseId +
        "&f_hnr=" + urlEncode(hnr.c_str()) + "&f_kdnr=";
    {
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/abfallarten/", sessionCookie, true);
        int code = http->POST(String(baseData.c_str()));
        Serial.printf("[WCL] abfallarten POST='%s'\n", baseData.c_str());
        Serial.printf("[WCL] abfallarten HTTP=%d size=%d ct='%s' te='%s'\n",
            code, http->getSize(),
            http->header("Content-Type").c_str(),
            http->header("Transfer-Encoding").c_str());
        logInfoP("abfallplus ch%d: abfallarten HTTP %d", _channelIndex, code);
        if (code == 200)
            streamExtractWasteTypesByKeywords(http, frKeywords, frMatchedIds, frMatchedNames);
        http->end(); delete http;
        logInfoP("abfallplus ch%d: keywords matched Fr1=%d Fr2=%d Fr3=%d Fr4=%d",
            _channelIndex, frMatchedIds[0], frMatchedIds[1], frMatchedIds[2], frMatchedIds[3]);
    }
    delay(1100);

    // Abbruch wenn kein einziges Keyword gematcht hat (spart weitere HTTP-Schritte)
    {
        bool anyMatched = false;
        for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
            if (frMatchedIds[f]) { anyMatched = true; break; }
        if (!anyMatched) {
            logInfoP("abfallplus ch%d: Kein Keyword konnte einer Abfallart zugeordnet werden - Abfallarten pruefen!", _channelIndex);
            _lastFetch = millis();
            return false;
        }
    }

    // ─── Schritt 7: assistent/ueberpruefen/ ───────────────────────────────
    std::string wasteData = baseData;
    for (int f = 0; f < WCL_NUM_FRACTIONS; f++) {
        if (frMatchedIds[f] == 0) continue;
        char typeStr[8];
        snprintf(typeStr, sizeof(typeStr), "%d", (int)frMatchedIds[f]);
        wasteData += std::string("&f_id_abfallart[]=") + typeStr;
    }
    wasteData += "&f_uhrzeit_tag=86400%7C0&f_uhrzeit_stunden=54000&f_uhrzeit_minuten=600&f_anonym=1&f_ausgangspunkt=1&f_ueberspringen=0";
    {
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/ueberpruefen/", sessionCookie, true);
        int code = http->POST(String(wasteData.c_str()));
        logInfoP("abfallplus ch%d: ueberpruefen HTTP %d", _channelIndex, code);
        http->end(); delete http;
    }
    delay(1100);

    // ─── Schritt 8: assistent/finish/ ─────────────────────────────────────
    {
        time_t now = time(nullptr);
        struct tm* t = localtime(&now);
        char ts[16];
        snprintf(ts, sizeof(ts), "%04d%02d%02d%02d%02d%02d",
            t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
            t->tm_hour, t->tm_min, t->tm_sec);
        std::string finishData = wasteData + "&f_datenschutz=" + ts;
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/assistent/finish/", sessionCookie, true);
        int code = http->POST(String(finishData.c_str()));
        logInfoP("abfallplus ch%d: finish HTTP %d", _channelIndex, code);
        http->end(); delete http;
    }
    delay(1100);

    // ─── Schritt 9+10: version.xml (Wizard-Abschluss, kurzer Timeout) ─────
    {
        std::string vData = std::string("client=") + clientId + "&app_id=" + appId;
#ifdef ARDUINO_ARCH_ESP32
        esp_task_wdt_reset();
#endif
        HTTPClient* h9 = makeAbfallHttp("https://app.abfallplus.de/version.xml", sessionCookie, false);
        h9->setTimeout(4000); // kurzer Timeout – Antwort wird nicht ausgewertet
        h9->POST(String(vData.c_str())); h9->end(); delete h9;
        delay(100);
#ifdef ARDUINO_ARCH_ESP32
        esp_task_wdt_reset();
#endif
        HTTPClient* h10 = makeAbfallHttp("https://app.abfallplus.de/version.xml?renew=1", sessionCookie, false);
        h10->setTimeout(4000);
        h10->POST(String(vData.c_str())); h10->end(); delete h10;
        delay(100);
    }

    // ─── Schritt 11: struktur.xml.zip → ZIP-Dekomprimierung + Plist-XML parsen ─
    {
        std::string vData = std::string("client=") + clientId + "&app_id=" + appId;
#ifdef ARDUINO_ARCH_ESP32
        esp_task_wdt_reset();
#endif
        HTTPClient* http = makeAbfallHttp("https://app.abfallplus.de/struktur.xml.zip", sessionCookie, false);
        int httpCode = http->POST(String(vData.c_str()));
        int zipSize = http->getSize();
        logInfoP("abfallplus ch%d: struktur HTTP %d len=%d", _channelIndex, httpCode, zipSize);
        if (httpCode != 200) {
            http->end(); delete http;
            _lastFetch = millis();
            return false;
        }

        // ZIP-Bytes aus Stream in Puffer lesen
        if (zipSize <= 0 || zipSize > 65536) zipSize = 16384;
        uint8_t* zipBuf = (uint8_t*)malloc(zipSize + 128);
        if (!zipBuf) {
            logInfoP("abfallplus ch%d: ZIP Speicher fehlt", _channelIndex);
            http->end(); delete http;
            _lastFetch = millis();
            return false;
        }
        {
            Stream* zs = &http->getStream();
            zs->setTimeout(10000);
            uint8_t rb[256];
            int n, zipLen = 0;
            while ((n = zs->readBytes(rb, sizeof(rb))) > 0 && zipLen < zipSize + 128)
            { memcpy(zipBuf + zipLen, rb, n); zipLen += n; }
            zipSize = zipLen;
        }
        http->end(); delete http;
#ifdef ARDUINO_ARCH_ESP32
        esp_task_wdt_reset();
#endif
        logInfoP("abfallplus ch%d: komprimiert gelesen %d bytes (sig=%02X%02X)", _channelIndex, zipSize, zipBuf[0], zipBuf[1]);

        // Deflate-Daten-Offset bestimmen (gzip oder ZIP)
        int dataOffset = 0;
        int compLen    = 0;
        if (zipSize >= 10 && zipBuf[0] == 0x1F && zipBuf[1] == 0x8B) {
            // gzip: Header mindestens 10 Bytes, dann optionale Felder
            dataOffset = 10;
            uint8_t flg = zipBuf[3];
            if (flg & 0x04) { // FEXTRA
                if (dataOffset + 2 > zipSize) { free(zipBuf); _lastFetch = millis(); return false; }
                uint16_t xlen = zipBuf[dataOffset] | ((uint16_t)zipBuf[dataOffset+1] << 8);
                dataOffset += 2 + xlen;
            }
            if (flg & 0x08) { // FNAME: null-terminated string
                while (dataOffset < zipSize && zipBuf[dataOffset] != 0) dataOffset++;
                dataOffset++; // skip null
            }
            if (flg & 0x10) { // FCOMMENT
                while (dataOffset < zipSize && zipBuf[dataOffset] != 0) dataOffset++;
                dataOffset++;
            }
            if (flg & 0x02) dataOffset += 2; // FHCRC
            compLen = zipSize - dataOffset - 8; // letzte 8 Bytes = CRC32 + ISIZE
            if (compLen <= 0) compLen = zipSize - dataOffset;
        } else if (zipSize >= 30 && zipBuf[0] == 0x50 && zipBuf[1] == 0x4B) {
            // ZIP Local File Header
            uint16_t fnLen  = zipBuf[26] | ((uint16_t)zipBuf[27] << 8);
            uint16_t extLen = zipBuf[28] | ((uint16_t)zipBuf[29] << 8);
            dataOffset = 30 + fnLen + extLen;
            compLen    = zipSize - dataOffset;
        } else {
            logInfoP("abfallplus ch%d: Unbekanntes Format", _channelIndex);
            free(zipBuf);
            _lastFetch = millis();
            return false;
        }
        if (dataOffset >= zipSize || compLen <= 0) {
            logInfoP("abfallplus ch%d: Header ungültig (off=%d comp=%d)", _channelIndex, dataOffset, compLen);
            free(zipBuf);
            _lastFetch = millis();
            return false;
        }

        // Dekompressions-Puffer aus PSRAM (ca. 150 KB für Plist-XML)
        const int XML_BUF = 160 * 1024;
#ifdef ARDUINO_ARCH_ESP32
        char* xmlBuf = (char*)heap_caps_malloc(XML_BUF, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!xmlBuf) xmlBuf = (char*)malloc(XML_BUF);
#else
        char* xmlBuf = (char*)malloc(XML_BUF);
#endif
        if (!xmlBuf) {
            logInfoP("abfallplus ch%d: XML-Puffer fehlt", _channelIndex);
            free(zipBuf);
            _lastFetch = millis();
            return false;
        }

        // miniz tinfl: raw deflate – Decompressor auf dem Heap (ca. 11 KB, Stack reicht nicht)
#ifdef ARDUINO_ARCH_ESP32
        tinfl_decompressor* decomp = (tinfl_decompressor*)malloc(sizeof(tinfl_decompressor));
        if (!decomp) {
            logInfoP("abfallplus ch%d: tinfl alloc failed", _channelIndex);
            free(zipBuf); free(xmlBuf);
            _lastFetch = millis();
            return false;
        }
        tinfl_init(decomp);
        size_t inBytes  = (size_t)compLen;
        size_t outBytes = (size_t)(XML_BUF - 1);
        tinfl_status tst = tinfl_decompress(decomp,
            (const mz_uint8*)(zipBuf + dataOffset), &inBytes,
            (mz_uint8*)xmlBuf, (mz_uint8*)xmlBuf, &outBytes,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        free(decomp);
        esp_task_wdt_reset();
        size_t xmlLen = outBytes;
        free(zipBuf);
        if (tst != TINFL_STATUS_DONE) {
            logInfoP("abfallplus ch%d: Inflate fehlgeschlagen (%d, out=%d)", _channelIndex, (int)tst, (int)xmlLen);
            free(xmlBuf);
            _lastFetch = millis();
            return false;
        }
#else
        free(zipBuf);
        size_t xmlLen = 0;
#endif
        xmlBuf[xmlLen] = '\0';
        logInfoP("abfallplus ch%d: struktur entpackt %d bytes", _channelIndex, (int)xmlLen);

        // Heutiges Datum für Tage-Berechnung
        time_t nowT = time(nullptr);
        struct tm* tmNow = localtime(&nowT);
        int todayY = tmNow->tm_year + 1900, todayM = tmNow->tm_mon + 1, todayD = tmNow->tm_mday;

        // Fractions zurücksetzen
        for (int f = 0; f < WCL_NUM_FRACTIONS; f++) {
            _fractions[f].daysUntilPickup = 255;
            _fractions[f].pickupToday = false;
            _fractions[f].pickupTomorrow = false;
            _fractions[f].name[0] = '\0';
        }

        // Erwartetes Kategorie-ID vorab berechnen
        char expectedCatIds[WCL_NUM_FRACTIONS][48];
        for (int f = 0; f < WCL_NUM_FRACTIONS; f++) {
            if (frMatchedIds[f] == 0) expectedCatIds[f][0] = '\0';
            else snprintf(expectedCatIds[f], sizeof(expectedCatIds[f]),
                "%s-%d-v0", landkreisId.c_str(), (int)frMatchedIds[f]);
        }

        // Plist-XML zeilenweise parsen (im Speicher)
        enum PlistExpect { ANY, EXPECT_CAT_STRING, EXPECT_PICKUP_STRING } expect = ANY;
        char catId[48] = "";
        int dateCount = 0;
        const char* xmlPtr = xmlBuf;
        char lineBuf[256];

        int lineCount = 0;
        while (*xmlPtr) {
#ifdef ARDUINO_ARCH_ESP32
            if (++lineCount % 500 == 0) esp_task_wdt_reset();
#endif
            // Nächste Zeile extrahieren
            const char* nl = strchr(xmlPtr, '\n');
            int lineLen = nl ? (int)(nl - xmlPtr) : (int)strlen(xmlPtr);
            if (lineLen >= (int)sizeof(lineBuf)) lineLen = (int)sizeof(lineBuf) - 1;
            memcpy(lineBuf, xmlPtr, lineLen);
            lineBuf[lineLen] = '\0';
            xmlPtr = nl ? nl + 1 : xmlPtr + lineLen;

            // Whitespace trimmen
            int n = lineLen;
            while (n > 0 && (lineBuf[n-1] == '\r' || lineBuf[n-1] == ' ' || lineBuf[n-1] == '\t')) n--;
            lineBuf[n] = '\0';
            const char* line = lineBuf;
            while (*line == ' ' || *line == '\t') line++;

            if (strcmp(line, "<key>category_id</key>") == 0) {
                expect = EXPECT_CAT_STRING;
            } else if (strcmp(line, "<key>pickup_date</key>") == 0) {
                expect = EXPECT_PICKUP_STRING;
            } else if (strncmp(line, "<string>", 8) == 0) {
                const char* val = line + 8;
                const char* valEnd = strstr(val, "</string>");
                if (!valEnd) { expect = ANY; continue; }

                if (expect == EXPECT_CAT_STRING) {
                    int len = (int)(valEnd - val);
                    if (len < (int)sizeof(catId)) { memcpy(catId, val, len); catId[len] = '\0'; }
                    expect = ANY;
                } else if (expect == EXPECT_PICKUP_STRING && catId[0]) {
                    int y = 0, m = 0, d = 0;
                    if (sscanf(val, "%4d-%2d-%2d", &y, &m, &d) == 3 && y > 2020) {
                        dateCount++;
                        struct tm te = {}; te.tm_year = y-1900; te.tm_mon = m-1; te.tm_mday = d; te.tm_hour = 12;
                        struct tm tt = {}; tt.tm_year = todayY-1900; tt.tm_mon = todayM-1; tt.tm_mday = todayD; tt.tm_hour = 12;
                        int diffDays = (int)((mktime(&te) - mktime(&tt)) / 86400);
                        if (diffDays >= 0) {
                            for (int f = 0; f < WCL_NUM_FRACTIONS; f++) {
                                if (expectedCatIds[f][0] && strcmp(catId, expectedCatIds[f]) == 0) {
                                    if (diffDays < _fractions[f].daysUntilPickup) {
                                        _fractions[f].daysUntilPickup = (uint8_t)(diffDays > 254 ? 254 : diffDays);
                                        _fractions[f].pickupToday    = (diffDays == 0);
                                        _fractions[f].pickupTomorrow = (diffDays == 1);
                                        utf8ToLatin1(frMatchedNames[f], _fractions[f].name, 15);
                                    }
                                }
                            }
                        }
                    }
                    catId[0] = '\0';
                    expect = ANY;
                } else {
                    expect = ANY;
                }
            } else if (strcmp(line, "<dict>") == 0 || strcmp(line, "</dict>") == 0) {
                catId[0] = '\0';
                expect = ANY;
            }
        }
        free(xmlBuf);
        logInfoP("abfallplus ch%d: %d Termine geparselt", _channelIndex, dateCount);
    }

    publishKos();
    _lastFetch = millis();
    return true;
}

// Latin-1 → UTF-8 konvertieren (ETS-Parameter sind Latin-1)
static std::string latin1ToUtf8(const std::string& s)
{
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s)
    {
        if (c < 0x80)
            out += (char)c;
        else
        {
            out += (char)(0xC0 | (c >> 6));
            out += (char)(0x80 | (c & 0x3F));
        }
    }
    return out;
}

// Convert UTF-8 encoded string to Latin-1 (ISO 8859-1), writing at most dstSize-1 chars.
// Characters outside Latin-1 (U+0100+) are replaced with '?'.
static void utf8ToLatin1(const char* src, char* dst, size_t dstSize)
{
    size_t di = 0;
    const uint8_t* s = (const uint8_t*)src;
    while (*s && di < dstSize - 1)
    {
        if ((*s & 0x80) == 0)
        {
            dst[di++] = (char)*s++;
        }
        else if ((*s & 0xE0) == 0xC0 && (*(s+1) & 0xC0) == 0x80)
        {
            uint32_t cp = (uint32_t)((*s & 0x1F) << 6) | (*(s+1) & 0x3F);
            s += 2;
            dst[di++] = (cp <= 0xFF) ? (char)(uint8_t)cp : '?';
        }
        else if ((*s & 0xF0) == 0xE0)
        {
            s += (*(s+1) & 0xC0) == 0x80 ? ((*(s+2) & 0xC0) == 0x80 ? 3 : 2) : 1;
            dst[di++] = '?';
        }
        else if ((*s & 0xF8) == 0xF0) { s += 4; dst[di++] = '?'; }
        else { s++; dst[di++] = '?'; }
    }
    dst[di] = '\0';
}

// ─────────────────────────────────────────────────────────────────────────────
// müll.io: POST mit Adress-Headern → JSON
// ─────────────────────────────────────────────────────────────────────────────
bool WasteCalendarChannel::fetchAndParseMuellIo()
{
    std::string street = latin1ToUtf8(ParamWCL_CHMuellStreetStr);
    std::string hnr    = latin1ToUtf8(ParamWCL_CHMuellHnrStr);
    std::string zip    = ParamWCL_CHMuellZipStr;  // PLZ: nur ASCII
    std::string city   = latin1ToUtf8(ParamWCL_CHMuellCityStr);

    if (street.empty() || zip.empty())
    {
        logInfoP("muell.io ch%d: Straße/PLZ fehlt", _channelIndex);
        _lastFetch = millis();
        return false;
    }

    HTTPClient* http = new HTTPClient();
#ifdef ARDUINO_ARCH_RP2040
    http->setInsecure();
#endif
    http->begin("https://xn--mll-hoa.io/api/fetch");  // müll.io (Punycode)
    http->setTimeout(15000);
    http->setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const char* collectHeaders[] = {"Location"};
    http->collectHeaders(collectHeaders, 1);
    http->addHeader("Content-Type", "text/plain");
    http->addHeader("X-Address-Street",      street.c_str());
    http->addHeader("X-Address-HouseNumber", hnr.c_str());
    http->addHeader("X-Address-Zip",         zip.c_str());
    http->addHeader("X-Address-City",        city.c_str());
    http->addHeader("X-Address-Country",     "DE");
    int httpCode = http->POST(String(""));
    logInfoP("muell.io ch%d: HTTP %d", _channelIndex, httpCode);
    if (httpCode != 200)
    {
        String location = http->header("Location");
        logInfoP("muell.io ch%d: Location: %s", _channelIndex, location.c_str());
        String errBody = http->getString();
        logInfoP("muell.io ch%d: body: %.100s", _channelIndex, errBody.c_str());
        http->end(); delete http;
        _lastFetch = millis();
        return false;
    }
    String body = http->getString();
    http->end(); delete http;
    logInfoP("muell.io ch%d: body %d B", _channelIndex, (int)body.length());

    // JSON parsen (ArduinoJson)
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err)
    {
        logInfoP("muell.io ch%d: JSON-Fehler: %s", _channelIndex, err.c_str());
        _lastFetch = millis();
        return false;
    }

    // Alle JSON-Keys und nextDays-Werte loggen (Diagnose)
    for (JsonPair kv : doc.as<JsonObject>())
    {
        JsonVariant nd = kv.value()["nextDays"];
        int ndVal = nd.is<int>() ? nd.as<int>() : (nd.is<const char*>() && nd.as<const char*>() ? atoi(nd.as<const char*>()) : -1);
        logInfoP("muell.io ch%d: key='%s' nextDays=%d", _channelIndex, kv.key().c_str(), ndVal);
    }

    // Fraktionen aus Dropdown-Auswahl befüllen
    // Enum-Wert → müll.io JSON-Schlüssel
    static const char* const keyMap[] = {
        nullptr,           // 0 = deaktiviert
        "residualWaste",   // 1 = Restmüll
        "bio",             // 2 = Biomüll
        "paper",           // 3 = Papier
        "reusableMaterials", // 4 = Wertstoffe
        "toxic",           // 5 = Schadstoffe
        "hedgeTreeTrimming", // 6 = Heckenschnitt
        "diaper",          // 7 = Windeln
        "christmasTree",   // 8 = Weihnachtsbaum
    };

    for (int f = 0; f < WCL_NUM_FRACTIONS; f++)
    {
        _fractions[f].daysUntilPickup = 255;
        _fractions[f].pickupToday     = false;
        _fractions[f].pickupTomorrow  = false;
        _fractions[f].name[0]         = '\0';

        uint8_t sel = 0;
        switch (f)
        {
            case 0: sel = ParamWCL_CHMuellFr1; break;
            case 1: sel = ParamWCL_CHMuellFr2; break;
            case 2: sel = ParamWCL_CHMuellFr3; break;
            case 3: sel = ParamWCL_CHMuellFr4; break;
        }
        if (sel == 0 || sel >= sizeof(keyMap) / sizeof(keyMap[0])) continue;

        const char* jsonKey = keyMap[sel];
        JsonVariant entry = doc[jsonKey];
        if (entry.isNull()) continue;

        // nextDays kann String oder Integer sein
        int nextDays = -1;
        JsonVariant nd = entry["nextDays"];
        if (nd.is<int>())
            nextDays = nd.as<int>();
        else if (nd.is<const char*>() && nd.as<const char*>() != nullptr)
            nextDays = atoi(nd.as<const char*>());

        if (nextDays < 0 || nextDays > 254) continue;

        _fractions[f].daysUntilPickup = (uint8_t)nextDays;
        _fractions[f].pickupToday     = (nextDays == 0);
        _fractions[f].pickupTomorrow  = (nextDays == 1);
        strncpy(_fractions[f].name, keyMap[sel], 14);
        _fractions[f].name[14] = '\0';
    }

    publishKos();
    _lastFetch = millis();
    return true;
}
