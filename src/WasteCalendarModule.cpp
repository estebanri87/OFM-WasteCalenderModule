#include "WasteCalendarModule.h"
#include "WasteCalendarChannel.h"

WasteCalendarModule::WasteCalendarModule()
    : WCLChannelOwnerModule(WCL_ChannelCount)
{
}

const std::string WasteCalendarModule::name()
{
    return "WasteCalendar";
}

const std::string WasteCalendarModule::version()
{
#ifdef MODULE_WasteCalendarModule_Version
    return MODULE_WasteCalendarModule_Version;
#else
    return "";
#endif
}

void WasteCalendarModule::showInformations()
{
}

void WasteCalendarModule::showHelp()
{
}

bool WasteCalendarModule::processCommand(const std::string cmd, bool diagnoseKo)
{
    return false;
}

OpenKNX::Channel* WasteCalendarModule::createChannel(uint8_t _channelIndex /* this parameter is used in macros, do not rename */)
{
    // Datenquelle 0 = Deaktiviert: keinen Kanal anlegen
    if (ParamWCL_CHMode == 0)
        return nullptr;

    // Suspendiert verhaelt sich wie "Datenquelle = Deaktiviert": der Kanal wird nicht
    // angelegt und ruft nichts ab. Seine Kommunikationsobjekte bleiben in der ETS
    // erhalten, weil deren Sichtbarkeit an der Datenquelle haengt, nicht an Suspendiert.
    if (ParamWCL_CHSuspended)
        return nullptr;

    return new WasteCalendarChannel(_channelIndex);
}

WasteCalendarModule openknxWasteCalendarModule;
