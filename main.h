#ifndef _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#define _INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_
#include <ISmmPlugin.h>
#include <sh_vector.h>
#include <stdio.h>
#include <fstream>
#include <iserver.h>
#include <entity2/entitysystem.h>
#include "igameevents.h"
#include "vector.h"
#include <deque>
#include <functional>
#include <utlstring.h>
#include <KeyValues.h>
#include "CCSPlayerController.h"
#include <ctime>
#include "time.h"
#include <complex>
#include <iomanip>
#include "metamod_oslink.h"
#include "CGameRules.h"
#include "schemasystem/schemasystem.h"
#include "include/menus.h"
#include "include/admin.h"
#include <condition_variable>
#include <thread>
#include "filesystem.h"
#include "../SchemaEntity/ctimer.h"
#include "nlohmann/json.hpp"
#include "include/admin.h"
#include "../../hl2sdk-root/hl2sdk-cs2/public/steam/isteamgameserver.h"
#include "../../hl2sdk-root/hl2sdk-cs2/public/steam/steam_gameserver.h"
#include "../../hl2sdk-root/hl2sdk-cs2/public/steam/isteamhttp.h"
#include "../../hl2sdk-root/hl2sdk-cs2/public/steam/isteamnetworkingsockets.h"
#include <iomanip>

using json = nlohmann::json;
using namespace std;

struct ReportData {
    string reason;
    string victimName;
    string reporterName;
    uint64 victimSID;
    uint64 reporterSID;
};

struct ReportReasonParams {
    string text;
};

class Reports final : public ISmmPlugin, public IMetamodListener {
public:
    bool Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late);
    bool Unload(char* error, size_t maxlen);
    void AllPluginsLoaded();
    void OnGameServerSteamAPIActivated();
private:
    const char* GetAuthor();
    const char* GetName();
    const char* GetDescription();
    const char* GetURL();
    const char* GetLicense();
    const char* GetVersion();
    const char* GetDate();
    const char* GetLogTag();
};

#endif //_INCLUDE_METAMOD_SOURCE_STUB_PLUGIN_H_