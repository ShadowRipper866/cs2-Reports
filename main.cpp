//
// Ported from CSS with love by ShadowRipper <3
//

#include "main.h"

#define MAX_PLAYERS 64
Reports g_reports;
PLUGIN_EXPOSE(Reports, g_reports);

IUtilsApi *utils;
IMenusApi *menus_api;
IPlayersApi *players_api;
IAdminApi *admin_api;
bool bDebug = false;
static bool pluginLoaded = false;

CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
IVEngineServer2* engine = nullptr;
CGlobalVars *gpGlobals = nullptr;
SH_DECL_HOOK0_void(IServerGameDLL, GameServerSteamAPIActivated, SH_NOATTRIB, 0);

bool isCustomReason[MAX_PLAYERS];
int pendingTarget[MAX_PLAYERS];
map<string, string> phrases;
vector<string> reportReasons;
map<int, ReportReasonParams> rParams;
const char* adminImmunityFlag;
string webhookDs;
bool enableCustomReason;
map<string, json> g_mQueue;

string hostname;
string ip;

int wColor;
vector<tuple<string, string, bool>> fields;
string wDescription;

CSteamGameServerAPIContext g_steamAPI;
ISteamHTTP *g_http = nullptr;

CGameEntitySystem* GameEntitySystem()
{
    return utils->GetCGameEntitySystem();
}

uint32_t hex_to_int(const string& hex_str) {
    std::string cleaned = hex_str;
    if (cleaned[0] == '#') {
        cleaned = cleaned.substr(1);
    }
    if (cleaned.length() != 6) {
        throw invalid_argument("Incorrect format: RRGGBB excepted.");
    }
    uint32_t result;
    std::stringstream ss;
    ss << hex << cleaned;
    if (!(ss >> result)) {
        throw invalid_argument("Incorrect HEX-format");
    }
    return result;
}

void SelectReasonMenu(int slot, int target);
void SendReport(int slot, int target, const char* reason);
void SendWebhookToDiscord(const char* content, int slot, int target, const char *reason, const char* vName, const char* rName);

void dbgmsg(string msg, ...) {
    if (bDebug) {
        char buf[1024];
        va_list va; va_start(va, msg);
        V_vsnprintf(buf, sizeof(buf), msg.c_str(), va);
        va_end(va);
        ConColorMsg(Color(0, 200, 255, 255), "[%s] %s\n", g_PLAPI->GetLogTag(), buf);
    }
}

void StartupServer()
{
    g_pGameEntitySystem = GameEntitySystem();
    g_pEntitySystem = utils->GetCEntitySystem();
    gpGlobals = utils->GetCGlobalVars();
    if (pluginLoaded) META_CONPRINTF("%s Plugin started successfully. Marking system ready.\n", g_PLAPI->GetLogTag());
}

class CWebhookCallback
{
public:
    CWebhookCallback(HTTPRequestHandle hRequest) : m_hRequest(hRequest) {}

    void OnHTTPRequestCompleted(HTTPRequestCompleted_t *pCallback, bool bIOFailure)
    {
        if (bIOFailure) dbgmsg("Webhook request failed: I/O failure");
        else if (pCallback->m_eStatusCode != 200 && pCallback->m_eStatusCode != 204) dbgmsg("Webhook request failed with status code: %d", pCallback->m_eStatusCode);
         else dbgmsg("Webhook sent successfully");

        if (g_http) {
            g_http->ReleaseHTTPRequest(m_hRequest);
        }
        delete this;
    }

private:
    HTTPRequestHandle m_hRequest;
};

void LoadConfig() {
    rParams.clear();
    reportReasons.clear();
    KeyValues *config = new KeyValues("Config");
    const char *path = "addons/configs/reports/reports.ini";
    if (!config->LoadFromFile(g_pFullFileSystem, path)) {
        utils->ErrorLog("[%s] Failed to load: %s", g_PLAPI->GetLogTag(), path);
        delete config;
        return;
    }
    KeyValues *reasons = config->FindKey("reasons");
    if (reasons) {
        FOR_EACH_TRUE_SUBKEY(reasons, value) {
            const char *name = value->GetName();
            if (name && name[0] != '\0') {
                reportReasons.push_back(name);
                int index = atoi(name);
                ReportReasonParams params;
                params.text = value->GetString("reasonText", "");
                rParams[index] = params;
            }
        }
    }
    KeyValues* hAdmin = config->FindKey("admin");
    if (hAdmin) adminImmunityFlag = strdup(hAdmin->GetString("immunity_flag", ""));
    bDebug = config->GetBool("debug_mode", false);
    webhookDs = config->GetString("webhook_link");
    enableCustomReason = config->GetBool("enableCustomReason", false);
    hostname = config->GetString("Webhook_ServerName");
    ip = config->GetString("server_ip");
    wColor = hex_to_int(config->GetString("Webhook_color"));
    wDescription = config->GetString("webhook_description");
    delete config;
}

bool CheckPrime(uint64 SteamID)
{
    CSteamID steamID(SteamID);
    return SteamGameServer()->UserHasLicenseForApp(steamID, 624820) == 0 ||
           SteamGameServer()->UserHasLicenseForApp(steamID, 54029) == 0;
}

const char* GetTranslation(const char* key) {
    return phrases[string(key)].c_str();
}

bool OnPlayerReportCommand(int slot, const char* content) {
    Menu menu;
    string reason = content;
    dbgmsg(reason);
    menus_api->SetTitleMenu(menu, GetTranslation("Menu_SelectPlayerTitle"));
    bool hasPlayers = false;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        bool shouldAdd = false;
        if (bDebug || (i != slot)) {
            const char *playerName = players_api->GetPlayerName(i);
            if (players_api->IsConnected(i)) {
                if (admin_api->HasPermission(i, adminImmunityFlag) && !bDebug) shouldAdd = false;
                else shouldAdd = true;
            }
            if (shouldAdd) {
                menus_api->AddItemMenu(menu, to_string(i).c_str(), playerName);
                hasPlayers = true;
            }
        }
    }
    if (!hasPlayers) menus_api->AddItemMenu(menu, "", admin_api->GetTranslation("NoPlayers"), ITEM_DISABLED);
    menus_api->SetBackMenu(menu, true);
    menus_api->SetExitMenu(menu, true);

    menus_api->SetCallback(menu, [](const char* szBack, const char* szFront, int iItem, int iSlot) {
        if (iItem < 7) {
            int target = atoi(szBack);
            pendingTarget[iSlot] = target;  // Set pending target here
            SelectReasonMenu(iSlot, target);
        }
        else if (iItem == 7) admin_api->ShowAdminLastCategoryMenu(iSlot);
    });
    menus_api->DisplayPlayerMenu(menu, slot, true, true);
    return true;
}

void SelectReasonMenu(int slot, int target) {
    Menu menu;
    menus_api->SetTitleMenu(menu, GetTranslation("Menu_Title"));
    for (auto& pair : rParams) {
        int key = pair.first;
        ReportReasonParams &params = pair.second;
        string id = to_string(key);
        const char *label = params.text.c_str();
        menus_api->AddItemMenu(menu, id.c_str(), label);
    }
    if (enableCustomReason) menus_api->AddItemMenu(menu, "custom", GetTranslation("Menu_customReason"));
    menus_api->SetBackMenu(menu, true);
    menus_api->SetExitMenu(menu, true);
    menus_api->SetCallback(menu, [target](const char* szBack, const char* szFront, int iItem, int iSlot) {
        if (szBack && strcmp(szBack, "custom") == 0) {
            isCustomReason[iSlot] = true;
            utils->PrintToChat(iSlot, GetTranslation("Player_customReasonGuide"));
            return;
        }
        if (iItem < 7) {
            if (!szBack || !szBack[0]) return;
            int reasonKey = atoi(szBack);
            auto it = rParams.find(reasonKey);
            if (it != rParams.end()) {
                const char* reasonText = it->second.text.c_str();
                dbgmsg(reasonText);
                SendReport(iSlot, target, reasonText);
            }
        }
        else if (iItem == 7) admin_api->ShowAdminLastCategoryMenu(iSlot);
    });
    menus_api->DisplayPlayerMenu(menu, slot, true, true);
}

void SendReport(int slot, int target, const char* reason) {
    const char* reporterName = players_api->GetPlayerName(slot);
    const char* victimName = players_api->GetPlayerName(target);

    if (!reporterName || !victimName) {
        utils->PrintToChat(slot, GetTranslation("PlayerNotFound"));
        return;
    }
    SendWebhookToDiscord(wDescription.c_str(), slot, target, reason, victimName, reporterName);
    utils->PrintToChat(slot, GetTranslation("Player_reportSentSuccessfully"));
}

void SendWebhookAsync(const string& webhookUrl, const string& jsonData) {
    if (!g_http) {
        dbgmsg("SteamHTTP not available");
        return;
    }
    HTTPRequestHandle hRequest = g_http->CreateHTTPRequest(k_EHTTPMethodPOST, webhookUrl.c_str());
    if (hRequest == INVALID_HTTPREQUEST_HANDLE) {
        dbgmsg("Failed to create HTTP request");
        return;
    }
    g_http->SetHTTPRequestHeaderValue(hRequest, "Content-Type", "application/json");
    g_http->SetHTTPRequestRawPostBody(hRequest, "application/json", (uint8*)jsonData.c_str(), jsonData.size());
    SteamAPICall_t hCall;
    if (g_http->SendHTTPRequest(hRequest, &hCall)) {
        CWebhookCallback *pCallback = new CWebhookCallback(hRequest);
        CCallResult<CWebhookCallback, HTTPRequestCompleted_t> *pCallResult = new CCallResult<CWebhookCallback, HTTPRequestCompleted_t>;
        pCallResult->Set(hCall, pCallback, &CWebhookCallback::OnHTTPRequestCompleted);
        dbgmsg("Webhook request sent successfully");
    }
    else {
        dbgmsg("Failed to send HTTP request");
        g_http->ReleaseHTTPRequest(hRequest);
    }
}

void SendWebhookToDiscord(const char* content, int slot, int target, const char *reason, const char* vName, const char* rName) {
    CCSPlayerController *player = CCSPlayerController::FromSlot(slot);
    CCSPlayerController *cTarget = CCSPlayerController::FromSlot(target);

    CCSPlayerController_ActionTrackingServices *rStats = player->m_pActionTrackingServices();
    CCSPlayerController_ActionTrackingServices *vStats = cTarget->m_pActionTrackingServices();

    int rDeaths = rStats->m_matchStats().m_iDeaths();
    int rKills = rStats->m_matchStats().m_iKills();
    int vDeaths = vStats->m_matchStats().m_iDeaths();
    int vKills = vStats->m_matchStats().m_iKills();

    float rKD = (rDeaths != 0) ? static_cast<float>(rKills) / rDeaths : rKills;
    rKD = round(rKD * 100.0f) / 100.0f;

    float vKD = (vDeaths != 0) ? static_cast<float>(vKills) / vDeaths : vKills;
    vKD = round(vKD * 100.0f) / 100.0f;

    char rBuffer[32];
    g_SMAPI->Format(rBuffer, sizeof(rBuffer), "%.2f", rKD);

    char vBuffer[32];
    g_SMAPI->Format(vBuffer, sizeof(vBuffer), "%.2f", vKD);

    string rPrime = CheckPrime(player->m_steamID) ? GetTranslation("Webhook_hasPrimeStatus") : GetTranslation("Webhook_noPrimeStatus");
    string vPrime = CheckPrime(cTarget->m_steamID) ? GetTranslation("Webhook_hasPrimeStatus") : GetTranslation("Webhook_noPrimeStatus");

    const char *vIp = players_api->GetIpAddress(target);

    try {
        json j;
        j["content"] = content;

        json embeds = json::array();
        json jemb;

        jemb["description"] = "## " + string(GetTranslation("Webhook_New_Report"));
        jemb["color"] = wColor;
        if (!hostname.empty()) {
            json author;
            author["name"] = hostname;
            jemb["author"] = author;
        }
        json jfields;

        json jField;
        jField["name"] = GetTranslation("Webhook_Reporter");
        jField["value"] = "[**`" + (string)rName + "`**](https://steamcommunity.com/profiles/" + to_string(player->m_steamID) + ")";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_PrimeStatus");
        jField["value"] = "```" + rPrime + "```";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_SteamId");
        jField["value"] = "```" + to_string(player->m_steamID) + "```";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_ReporterStatistic");
        jField["value"] = "```" + (string)"KD: " + rBuffer + "\n(" + to_string(rKills) + " / " + to_string(rDeaths) + ")" + "```";
        jField["inline"] = false;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_Victim");
        jField["value"] = "[**`" + (string)vName + "`**](https://steamcommunity.com/profiles/" + to_string(cTarget->m_steamID) + ")";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_PrimeStatus");
        jField["value"] = "```" + vPrime + "```";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_SteamId");
        jField["value"] = "```" + to_string(cTarget->m_steamID) + "```";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_VictimStatistic");
        jField["value"] = "```" + (string)"KD: " + vBuffer + "\n(" + to_string(vKills) + " / " + to_string(vDeaths) + ")" +  "```";
        jField["inline"] = false;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_Reason");
        jField["value"] = "```" + (string)reason + "```";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_Map");
        jField["value"] = "```" + (string)gpGlobals->mapname.ToCStr() + "```";
        jField["inline"] = true;
        jfields.push_back(jField);

        jField = json();
        jField["name"] = GetTranslation("Webhook_VictimIP");
        jField["value"] = "```" + (string)vIp + "```";
        jField["inline"] = true;
        jfields.push_back(jField);
        if (!ip.empty()) {
            jField = json();
            jField["name"] = GetTranslation("Webhook_ServerConnect");
            jField["value"] = "```" + (string)"connect " + ip + "```";
            jField["inline"] = false;
            jfields.push_back(jField);
        }
        jemb["fields"] = jfields;
        embeds.push_back(jemb);

        j["embeds"] = embeds;

        string jsonStr = j.dump();
        dbgmsg("DATA: %s", jsonStr.c_str());
        SendWebhookAsync(webhookDs, jsonStr);

        dbgmsg("Webhook queued for sending: %s reported %s for %s", rName, vName, reason);

    }
    catch (const exception& e) {
        utils->ErrorLog("[%s] Exception in SendWebhookToDiscord: %s", g_PLAPI->GetLogTag(), e.what());
    }
}

void OnPlayerCustomReason(const char* szName, IGameEvent* event, bool bDontBroadcast) {
    int slot = event->GetInt("userid");
    if (!isCustomReason[slot]) return;
    string input = event->GetString("text");
    if (input.empty()) {
        utils->PrintToChat(slot, GetTranslation("NoReasonProvided"));
        isCustomReason[slot] = false;
        pendingTarget[slot] = -1;
        return;
    }
    int target = pendingTarget[slot];
    if (!players_api->IsConnected(target)) {
        utils->PrintToChat(slot, GetTranslation("InvalidTarget"));
        isCustomReason[slot] = false;
        pendingTarget[slot] = -1;
        return;
    }
    string msg = input.substr(1);
    SendReport(slot, target, msg.c_str());
    isCustomReason[slot] = false;
    pendingTarget[slot] = -1;
}

void OnPlayerConnect(const char* szName, IGameEvent* pEvent, bool bDontBroadcast) {
    int iSlot = pEvent->GetInt("userid");
    isCustomReason[iSlot] = false;
    pendingTarget[iSlot] = -1;
}

void Reports::OnGameServerSteamAPIActivated()
{
    g_steamAPI.Init();
    g_http = g_steamAPI.SteamHTTP();
    if (g_http) dbgmsg("SteamHTTP initialized successfully");
    else dbgmsg("Failed to get SteamHTTP");
    RETURN_META(MRES_IGNORED);
}

CON_COMMAND_F(mm_reports_config_reload, "", FCVAR_SERVER_CAN_EXECUTE) {
    LoadConfig();
    META_CONPRINTF("%s Configuration reloaded successfully.\n", g_PLAPI->GetLogTag());
}

bool Reports::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);
    GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2Server, ISource2Server, SOURCE2SERVER_INTERFACE_VERSION);
    GET_V_IFACE_ANY(GetServerFactory, g_pSource2GameClients, IServerGameClients, SOURCE2GAMECLIENTS_INTERFACE_VERSION);

    SH_ADD_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &Reports::OnGameServerSteamAPIActivated), false);

    ConVar_Register(FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    g_SMAPI->AddListener(this, this);

    LoadConfig();

    return true;
}

void Reports::AllPluginsLoaded()
{
    int ret;
    char error[64] = { 0 };
    utils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        V_strncpy(error, "Missing Utils system plugin", 64);
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", g_PLAPI->GetLogTag(), error);
        string sBuffer = "meta unload "+to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }
    players_api = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        utils->ErrorLog("[%s] Missing Players system plugin", g_reports.GetLogTag());
        string sBuffer = "meta unload "+to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }
    menus_api = (IMenusApi *)g_SMAPI->MetaFactory(Menus_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        utils->ErrorLog("[%s] Missing Menus system plugin", g_PLAPI->GetLogTag());
        ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
        string sBuffer = "meta unload "+to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }
    admin_api = (IAdminApi *)g_SMAPI->MetaFactory(Admin_INTERFACE, &ret, NULL);
    if (ret == META_IFACE_FAILED)
    {
        utils->ErrorLog("[%s] Missing Admin system plugin", g_PLAPI->GetLogTag());
        string sBuffer = "meta unload "+to_string(g_PLID);
        engine->ServerCommand(sBuffer.c_str());
        return;
    }
    utils->StartupServer(g_PLID, StartupServer);
    LoadConfig();
    utils->RegCommand(g_PLID, {"mm_report"}, {"!report"}, OnPlayerReportCommand);
    utils->HookEvent(g_PLID, "player_chat", OnPlayerCustomReason);
    utils->HookEvent(g_PLID, "player_connect_full", OnPlayerConnect);
    players_api->HookOnClientAuthorized(g_PLID, [](int slot, uint64 sid)
    {
        isCustomReason[slot] = false;
        KeyValues* g_kvPhrases = new KeyValues("Phrases");
        const char *pszPath = "addons/translations/reports.phrases.txt";

        if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
        {
            utils->ErrorLog("[%s] Failed to load %s", g_PLAPI->GetLogTag(), pszPath);
            return;
        }

        const char* language = utils->GetLanguage();
        for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey()) {
            phrases[string(pKey->GetName())] = string(pKey->GetString(language));
        }
        delete g_kvPhrases;
    });
    pluginLoaded = true;
}

bool Reports::Unload(char *error, size_t maxlen)
{
    SH_REMOVE_HOOK(IServerGameDLL, GameServerSteamAPIActivated, g_pSource2Server, SH_MEMBER(this, &Reports::OnGameServerSteamAPIActivated), false);
    g_steamAPI.Clear();
    utils->ClearAllHooks(g_PLID);
    ConVar_Unregister();
    return true;
}

const char *Reports::GetAuthor() {
    return "ShadowRipper";
}
const char *Reports::GetDate() {
    return __DATE__;
}
const char *Reports::GetDescription() {
    return "Simple plugin for reports";
}
const char *Reports::GetLicense() {
    return "Free";
}
const char *Reports::GetLogTag() {
    return "[REPORTS]";
}
const char *Reports::GetName() {
    return "Report System";
}
const char *Reports::GetURL() {
    return "";
}
const char *Reports::GetVersion() {
    return "1.0";
}