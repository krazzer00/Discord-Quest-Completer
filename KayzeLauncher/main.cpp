#define _CRT_SECURE_NO_WARNINGS
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <vector>
#include <string>
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <dwmapi.h>
#include <fstream>
#include <urlmon.h>
#include <wininet.h>
#include <cstdlib>
#include <direct.h>
#include <sstream>
#include <deque>
#include <ctime>
#include <iterator>
#include <wincrypt.h>

#include "font_data.h"
#include "json.hpp"
#include "qrcodegen.h"
#include "remote_auth.h"
using json = nlohmann::json;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "urlmon.lib")
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "crypt32.lib")

#define IDI_ICON1 101 
static bool g_CanDrag = true;

const char* DB_URL = "https://cdn.discordapp.com/detectables/games.json";
const char* DB_FILENAME = "games_cache.temp";
const char* CONFIG_FILENAME = "kayze.config";
const char* QUESTS_URL = "https://discord.com/api/v9/quests/@me";
const char* APPS_URL = "https://discord.com/api/v9/applications/public?application_ids=";
const char* TOKEN_FILENAME = "discord_token.dat";

const std::string CURRENT_VERSION = "1.2.0";

std::string GetGitHubAPIResponse(const char* url) {
    HINTERNET hInternet = InternetOpenA("KayzeLauncher_Updater/1.0", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return "";
    HINTERNET hConnect = InternetOpenUrlA(hInternet, url, NULL, 0, INTERNET_FLAG_RELOAD, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return ""; }

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return response;
}

void CheckForUpdates() {
    std::string apiResponse = GetGitHubAPIResponse("https://api.github.com/repos/krazzer00/Discord-Quest-Completer/releases/latest");
    if (apiResponse.empty()) return;

    try {
        json releaseData = json::parse(apiResponse);
        std::string latestVersion = releaseData.value("tag_name", "");

        if (!latestVersion.empty() && (latestVersion[0] == 'v' || latestVersion[0] == 'V')) {
            latestVersion = latestVersion.substr(1);
        }

        if (latestVersion > CURRENT_VERSION && !latestVersion.empty()) {
            std::string downloadUrl = "";
            if (releaseData.contains("assets") && releaseData["assets"].is_array()) {
                for (auto& asset : releaseData["assets"]) {
                    std::string assetName = asset.value("name", "");
                    if (assetName.find(".exe") != std::string::npos) {
                        downloadUrl = asset.value("browser_download_url", "");
                        break;
                    }
                }
            }

            if (!downloadUrl.empty()) {
                char myPath[MAX_PATH];
                GetModuleFileNameA(NULL, myPath, MAX_PATH);
                std::string currentExe(myPath);
                std::string oldExe = currentExe + ".old";
                std::string newExe = currentExe + ".new";

                HRESULT hr = URLDownloadToFileA(NULL, downloadUrl.c_str(), newExe.c_str(), 0, NULL);
                if (SUCCEEDED(hr)) {
                    MoveFileExA(currentExe.c_str(), oldExe.c_str(), MOVEFILE_REPLACE_EXISTING);
                    MoveFileExA(newExe.c_str(), currentExe.c_str(), MOVEFILE_REPLACE_EXISTING);

                    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
                    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
                    CreateProcessA(NULL, (LPSTR)currentExe.c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);

                    exit(0);
                }
            }
        }
    }
    catch (...) {}
}

void DeleteDirectoryRecursively(const std::string& directory) {
    std::string pattern = directory + "\\*.*";
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);

    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            std::string fileName = fd.cFileName;
            if (fileName != "." && fileName != "..") {
                std::string filePath = directory + "\\" + fileName;

                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                    DeleteDirectoryRecursively(filePath);
                }
                else {
                    SetFileAttributesA(filePath.c_str(), FILE_ATTRIBUTE_NORMAL);
                    DeleteFileA(filePath.c_str());
                }
            }
        } while (FindNextFileA(hFind, &fd));
        FindClose(hFind);
    }

    SetFileAttributesA(directory.c_str(), FILE_ATTRIBUTE_NORMAL);
    RemoveDirectoryA(directory.c_str());
}

void CleanupTempFiles() {
    SetFileAttributesA(DB_FILENAME, FILE_ATTRIBUTE_NORMAL);
    DeleteFileA(DB_FILENAME);

    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t lastSlash = path.find_last_of("\\/");
    std::string rootDir = path.substr(0, lastSlash);

    std::string fakeGamesDir = rootDir + "\\fake_games";
    DeleteDirectoryRecursively(fakeGamesDir);
}

const ImVec4 colTextLight = ImVec4(0.95f, 0.96f, 0.98f, 1.0f);
const ImVec4 colTextGrey = ImVec4(1.00f, 1.00f, 1.00f, 0.40f);
const ImVec4 colCyan = ImVec4(0.06f, 0.72f, 0.83f, 1.0f);
const ImVec4 colBoxBg = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
const ImVec4 colBorder = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
const ImVec4 colBtnStop = ImVec4(0.6f, 0.1f, 0.1f, 0.8f);

struct GameInfo {
    std::string name;
    std::string id;
    std::vector<std::string> executables;
    std::vector<std::string> executablePaths;
    bool guessedExe = false;
};

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

void AddWin32Executables(GameInfo& game, const json& executables) {
    if (!executables.is_array()) return;
    for (auto& exeEntry : executables) {
        if (!exeEntry.is_object() || !exeEntry.contains("os") || !exeEntry["os"].is_string() || exeEntry["os"] != "win32") continue;
        if (!exeEntry.contains("name") || !exeEntry["name"].is_string()) continue;
        std::string exePath = exeEntry["name"].get<std::string>();
        if (exePath.empty()) continue;

        std::string exeName = exePath;
        size_t lastSlash = exeName.find_last_of("/\\");
        if (lastSlash != std::string::npos) exeName = exeName.substr(lastSlash + 1);

        bool exists = false;
        for (size_t i = 0; i < game.executables.size(); ++i) {
            if (game.executables[i] == exeName && game.executablePaths[i] == exePath) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            game.executables.push_back(exeName);
            game.executablePaths.push_back(exePath);
        }
    }
}

// "Some Game: Remastered" -> "SomeGameRemastered", used when Discord gives no executable for a quest game.
std::string SanitizeExeName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.') out += (char)c;
    }
    return out.empty() ? "Game" : out;
}

std::vector<GameInfo> FetchGameDatabase() {
    std::vector<GameInfo> db;
    DeleteUrlCacheEntryA(DB_URL);
    HRESULT hr = URLDownloadToFileA(NULL, DB_URL, DB_FILENAME, 0, NULL);
    SetFileAttributesA(DB_FILENAME, FILE_ATTRIBUTE_HIDDEN);

    std::ifstream f(DB_FILENAME);
    if (f.is_open()) {
        try {
            json data = json::parse(f);
            if (data.is_array()) {
                for (auto& item : data) {
                    GameInfo game;
                    game.name = item.value("name", "Unknown");

                    if (item["id"].is_string()) game.id = item.value("id", "0");
                    else if (item["id"].is_number()) game.id = std::to_string(item["id"].get<long long>());
                    else game.id = "0";

                    if (item.contains("executables")) AddWin32Executables(game, item["executables"]);
                    if (!game.executables.empty()) db.push_back(game);
                }
            }
        }
        catch (...) { db.push_back({ "Database Error", "0", {}, {} }); }
        f.close();
    }
    else { db.push_back({ "Connection Error", "0", {}, {} }); }
    return db;
}

struct QuestInfo {
    std::string id;
    std::string questName;
    std::string gameName;
    std::string applicationId;
    std::string taskType;
    int targetSeconds = 0;
    int progressSeconds = 0;
    bool enrolled = false;
    bool guessedExe = false;
    GameInfo* game = nullptr;
};

std::string JsonStr(const json& obj, const char* key) {
    if (!obj.is_object() || !obj.contains(key)) return "";
    const json& v = obj[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer()) return std::to_string(v.get<long long>());
    return "";
}

std::string TrimToken(std::string s) {
    const std::string junk = " \t\r\n\"'";
    size_t start = s.find_first_not_of(junk);
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(junk);
    return s.substr(start, end - start + 1);
}

std::string GetTokenFilePath() {
    const char* appData = getenv("APPDATA");
    if (!appData) return TOKEN_FILENAME;
    std::string dir = std::string(appData) + "\\KayzeLauncher";
    _mkdir(dir.c_str());
    return dir + "\\" + TOKEN_FILENAME;
}

// The token is encrypted with DPAPI, so only the current Windows user can read it back.
bool SaveDiscordToken(const std::string& token) {
    DATA_BLOB in, out;
    in.pbData = (BYTE*)token.data(); in.cbData = (DWORD)token.size();
    if (!CryptProtectData(&in, L"KAYZE Discord Token", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return false;
    std::ofstream f(GetTokenFilePath(), std::ios::binary);
    bool ok = f.is_open();
    if (ok) f.write((const char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return ok;
}

std::string LoadDiscordToken() {
    std::ifstream f(GetTokenFilePath(), std::ios::binary);
    if (!f.is_open()) return "";
    std::string blob((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (blob.empty()) return "";
    DATA_BLOB in, out;
    in.pbData = (BYTE*)blob.data(); in.cbData = (DWORD)blob.size();
    if (!CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out)) return "";
    std::string token((const char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return token;
}

void ForgetDiscordToken() {
    DeleteFileA(GetTokenFilePath().c_str());
}

std::string DiscordApiGet(const char* url, const std::string& token, DWORD& statusCode) {
    statusCode = 0;
    HINTERNET hInternet = InternetOpenA("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9200 Chrome/134.0.6998.205 Electron/35.3.0 Safari/537.36", INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
    if (!hInternet) return "";
    std::string headers = "Authorization: " + token + "\r\nAccept: application/json\r\n";
    HINTERNET hConnect = InternetOpenUrlA(hInternet, url, headers.c_str(), (DWORD)headers.size(), INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_NO_COOKIES, 0);
    if (!hConnect) { InternetCloseHandle(hInternet); return ""; }

    DWORD len = sizeof(statusCode);
    HttpQueryInfoA(hConnect, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &statusCode, &len, NULL);

    std::string response;
    char buffer[4096];
    DWORD bytesRead = 0;
    while (InternetReadFile(hConnect, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
        response.append(buffer, bytesRead);
    }
    InternetCloseHandle(hConnect);
    InternetCloseHandle(hInternet);
    return response;
}

std::string UtcNowIso() {
    time_t now = time(nullptr);
    tm utc;
    gmtime_s(&utc, &now);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &utc);
    return buf;
}

bool IsDesktopQuest(const QuestInfo& quest) {
    return quest.taskType == "PLAY_ON_DESKTOP" || quest.taskType == "STREAM_ON_DESKTOP";
}

std::string QuestTaskLabel(const std::string& taskType) {
    if (taskType == "PLAY_ON_DESKTOP") return "Play";
    if (taskType == "STREAM_ON_DESKTOP") return "Stream in VC";
    if (taskType.find("WATCH_VIDEO") != std::string::npos) return "Video (not supported)";
    if (taskType == "PLAY_ACTIVITY") return "Activity (not supported)";
    return taskType.empty() ? "Unknown task" : taskType;
}

// Finds the game of a quest that isn't in the detectables db: asks Discord for the application's executables,
// and if Discord knows none, falls back to "<Game Name>.exe". Results live in extraGames so pointers stay valid.
GameInfo* ResolveUnknownGame(const QuestInfo& quest, const std::string& token, std::deque<GameInfo>& extraGames, bool& guessed) {
    guessed = false;
    for (auto& g : extraGames) {
        if ((!quest.applicationId.empty() && g.id == quest.applicationId) || ToLower(g.name) == ToLower(quest.gameName)) {
            guessed = g.guessedExe;
            return &g;
        }
    }

    GameInfo game;
    game.name = quest.gameName;
    game.id = quest.applicationId.empty() ? "0" : quest.applicationId;

    if (!quest.applicationId.empty()) {
        DWORD status = 0;
        std::string body = DiscordApiGet((std::string(APPS_URL) + quest.applicationId).c_str(), token, status);
        if (status == 200) {
            try {
                json apps = json::parse(body);
                if (apps.is_object()) apps = json::array({ apps });
                for (auto& app : apps) {
                    if (JsonStr(app, "id") != quest.applicationId) continue;
                    if (app.contains("executables")) AddWin32Executables(game, app["executables"]);
                    std::string appName = JsonStr(app, "name");
                    if (!appName.empty()) game.name = appName;
                }
            }
            catch (...) {}
        }
    }

    if (game.executables.empty()) {
        std::string exe = SanitizeExeName(game.name) + ".exe";
        game.executables.push_back(exe);
        game.executablePaths.push_back(exe);
        game.guessedExe = true;
        guessed = true;
    }

    extraGames.push_back(game);
    return &extraGames.back();
}

// Returns the active (not expired, not completed) quests of the account and links each one to its game in the detectables db.
std::vector<QuestInfo> FetchActiveQuests(const std::string& token, std::vector<GameInfo>& db, std::deque<GameInfo>& extraGames, std::string& error) {
    std::vector<QuestInfo> quests;
    error.clear();

    DWORD status = 0;
    std::string body = DiscordApiGet(QUESTS_URL, token, status);
    if (status == 401) { error = "Invalid Discord token (401)."; return quests; }
    if (status != 200 || body.empty()) {
        error = status == 0 ? "Can't reach Discord." : "Discord API error (HTTP " + std::to_string(status) + ").";
        return quests;
    }

    static const char* taskOrder[] = { "PLAY_ON_DESKTOP", "STREAM_ON_DESKTOP", "PLAY_ACTIVITY", "WATCH_VIDEO", "WATCH_VIDEO_ON_MOBILE" };
    std::string nowIso = UtcNowIso();

    try {
        json data = json::parse(body);
        json list = data.is_array() ? data : (data.contains("quests") ? data["quests"] : json::array());
        if (!list.is_array()) list = json::array();

        for (auto& q : list) {
            if (!q.contains("config") || !q["config"].is_object()) continue;
            const json& cfg = q["config"];

            std::string expiresAt = JsonStr(cfg, "expires_at");
            if (!expiresAt.empty() && expiresAt.substr(0, 19) < nowIso) continue;

            QuestInfo quest;
            quest.id = JsonStr(q, "id");
            if (cfg.contains("application") && cfg["application"].is_object()) {
                quest.applicationId = JsonStr(cfg["application"], "id");
                quest.gameName = JsonStr(cfg["application"], "name");
            }
            if (cfg.contains("messages") && cfg["messages"].is_object()) {
                quest.questName = JsonStr(cfg["messages"], "quest_name");
                std::string gameTitle = JsonStr(cfg["messages"], "game_title");
                if (!gameTitle.empty()) quest.gameName = gameTitle;
            }
            if (quest.gameName.empty()) quest.gameName = quest.questName.empty() ? "Unknown game" : quest.questName;

            const json* tasks = nullptr;
            for (const char* key : { "task_config_v2", "task_config" }) {
                if (cfg.contains(key) && cfg[key].is_object() && cfg[key].contains("tasks") && cfg[key]["tasks"].is_object()) {
                    tasks = &cfg[key]["tasks"];
                    break;
                }
            }
            if (tasks) {
                for (const char* t : taskOrder) {
                    if (tasks->contains(t)) { quest.taskType = t; break; }
                }
                if (quest.taskType.empty() && !tasks->empty()) quest.taskType = tasks->begin().key();
                if (!quest.taskType.empty()) {
                    const json& task = (*tasks)[quest.taskType];
                    if (task.is_object() && task.contains("target") && task["target"].is_number()) quest.targetSeconds = (int)task["target"].get<double>();
                }
            }

            if (q.contains("user_status") && q["user_status"].is_object()) {
                const json& us = q["user_status"];
                if (!JsonStr(us, "completed_at").empty()) continue;
                quest.enrolled = !JsonStr(us, "enrolled_at").empty();
                if (us.contains("progress") && us["progress"].is_object() && us["progress"].contains(quest.taskType)) {
                    const json& p = us["progress"][quest.taskType];
                    if (p.is_object() && p.contains("value") && p["value"].is_number()) quest.progressSeconds = (int)p["value"].get<double>();
                }
            }

            for (auto& g : db) {
                if (!quest.applicationId.empty() && g.id == quest.applicationId) { quest.game = &g; break; }
            }
            if (!quest.game) {
                std::string wanted = ToLower(quest.gameName);
                for (auto& g : db) {
                    if (ToLower(g.name) == wanted && !g.executables.empty()) { quest.game = &g; break; }
                }
            }
            if (!quest.game && IsDesktopQuest(quest)) quest.game = ResolveUnknownGame(quest, token, extraGames, quest.guessedExe);

            quests.push_back(quest);
        }
    }
    catch (...) { error = "Can't parse Discord quests response."; }

    std::stable_sort(quests.begin(), quests.end(), [](const QuestInfo& a, const QuestInfo& b) {
        bool aLaunchable = IsDesktopQuest(a) && a.game != nullptr;
        bool bLaunchable = IsDesktopQuest(b) && b.game != nullptr;
        return aLaunchable && !bLaunchable;
    });
    return quests;
}

std::string FitText(const std::string& text, float maxWidth) {
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) return text;
    std::string s = text;
    while (!s.empty() && ImGui::CalcTextSize((s + "...").c_str()).x > maxWidth) {
        s.pop_back();
        while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    }
    return s + "...";
}

void TextCentered(std::string text) {
    auto windowWidth = ImGui::GetContentRegionAvail().x;
    auto windowHeight = ImGui::GetContentRegionAvail().y;
    auto textWidth = ImGui::CalcTextSize(text.c_str()).x;
    auto textHeight = ImGui::CalcTextSize(text.c_str()).y;
    ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
    ImGui::SetCursorPosY((windowHeight - textHeight) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.4f), "%s", text.c_str());
}

LRESULT CALLBACK GhostProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (uMsg == WM_DESTROY) PostQuitMessage(0);
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        FillRect(hdc, &ps.rcPaint, (HBRUSH)GetStockObject(BLACK_BRUSH));
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

void RunAsFakeGame(std::string windowTitle) {
    const char* CLASS_NAME = "KayzeGhostClass";
    WNDCLASSEXA wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = GhostProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = LoadIcon(wc.hInstance, MAKEINTRESOURCE(IDI_ICON1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    if (!wc.hIconSm) wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(0, CLASS_NAME, windowTitle.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, NULL, NULL, GetModuleHandle(NULL), NULL);

    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg = { };
    while (GetMessageA(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
}

struct RunningGame {
    GameInfo info;
    std::string activeExePath;
    PROCESS_INFORMATION pi = { 0 };
    bool isRunning = false;
};

std::string GetCurrentDirectoryString() {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string path(buffer);
    size_t lastSlash = path.find_last_of("\\/");
    return path.substr(0, lastSlash);
}

std::string SanitizeDirectoryName(std::string name) {
    const std::string invalidChars = "\\/:*?\"<>|";
    for (char& c : name) {
        if (invalidChars.find(c) != std::string::npos) {
            c = '_';
        }
    }
    return name;
}

bool StartGhostProcess(RunningGame& game, std::string chosenExe, std::string relativeExePath) {
    std::string rootDir = GetCurrentDirectoryString();
    std::string fakeGamesDir = rootDir + "\\fake_games";
    _mkdir(fakeGamesDir.c_str());

    std::string safeGameName = SanitizeDirectoryName(game.info.name);
    std::string gameDir = fakeGamesDir + "\\" + safeGameName;
    _mkdir(gameDir.c_str());

    std::string normalizedPath = relativeExePath;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '/', '\\');

    std::string exeDir = gameDir;
    size_t lastSlash = normalizedPath.find_last_of('\\');
    if (lastSlash != std::string::npos) {
        std::string subDirs = normalizedPath.substr(0, lastSlash);
        std::stringstream ss(subDirs);
        std::string part;
        while (std::getline(ss, part, '\\')) {
            if (part.empty()) continue;
            exeDir += "\\" + part;
            _mkdir(exeDir.c_str());
        }
    }

    std::string destExe = exeDir + "\\" + chosenExe;
    std::string configFile = exeDir + "\\" + CONFIG_FILENAME;

    char myPath[MAX_PATH];
    GetModuleFileNameA(NULL, myPath, MAX_PATH);

    if (!CopyFileA(myPath, destExe.c_str(), FALSE)) return false;

    std::ofstream cfg(configFile);
    if (cfg.is_open()) {
        cfg << game.info.name;
        cfg.close();
    }

    STARTUPINFOA si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    ZeroMemory(&game.pi, sizeof(game.pi));

    std::string cmdLine = "\"" + destExe + "\"";

    if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, 0, NULL, (LPCSTR)exeDir.c_str(), &si, &game.pi)) {
        game.isRunning = true;
        game.activeExePath = destExe;
        return true;
    }
    return false;
}

void StopGhostProcess(RunningGame& game) {
    if (game.isRunning) {
        TerminateProcess(game.pi.hProcess, 0); WaitForSingleObject(game.pi.hProcess, 2000);
        CloseHandle(game.pi.hProcess); CloseHandle(game.pi.hThread);

        std::string exePath = game.activeExePath;

        size_t lastSlash = exePath.find_last_of("\\/");
        std::string dirPath = exePath.substr(0, lastSlash);
        std::string cfgPath = dirPath + "\\" + CONFIG_FILENAME;

        for (int i = 0; i < 10; i++) {
            DeleteFileA(exePath.c_str());
            DeleteFileA(cfgPath.c_str());
            Sleep(100);
            if (GetFileAttributesA(exePath.c_str()) == INVALID_FILE_ATTRIBUTES) break;
        }
        game.isRunning = false;
    }
}

void SetupKayzeStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f; style.FrameRounding = 12.0f; style.ChildRounding = 12.0f;
    style.PopupRounding = 12.0f; style.ScrollbarRounding = 12.0f; style.GrabRounding = 12.0f;
    style.WindowPadding = ImVec2(15, 15); style.ItemSpacing = ImVec2(12, 12);
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f); style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f; style.FrameBorderSize = 0.0f;
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0); colors[ImGuiCol_FrameBg] = colBoxBg;
    colors[ImGuiCol_ChildBg] = colBoxBg; colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.12f, 0.14f, 0.98f);
    colors[ImGuiCol_Border] = colBorder; colors[ImGuiCol_Text] = colTextLight;
    colors[ImGuiCol_TextDisabled] = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.11f, 0.50f, 0.87f, 0.6f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.06f, 0.72f, 0.83f, 0.8f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.06f, 0.72f, 0.83f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.11f, 0.50f, 0.87f, 0.5f); colors[ImGuiCol_HeaderHovered] = colCyan;
    colors[ImGuiCol_Tab] = colBoxBg; colors[ImGuiCol_TabHovered] = ImVec4(0.06f, 0.72f, 0.83f, 0.8f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.11f, 0.50f, 0.87f, 0.6f); colors[ImGuiCol_TabSelectedOverline] = colCyan;
    colors[ImGuiCol_TabDimmed] = colBoxBg; colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.11f, 0.50f, 0.87f, 0.4f);
}

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Renders a single horizontally-centered line of grey text.
void TextCenteredLine(const std::string& text) {
    float avail = ImGui::GetContentRegionAvail().x;
    float tw = ImGui::CalcTextSize(text.c_str()).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - tw) * 0.5f);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.75f), "%s", text.c_str());
}

// Encodes text as a QR code and draws it centered, on a white quiet-zone background.
void DrawQrCentered(const std::string& text, float sizePx) {
    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
    static uint8_t tmp[qrcodegen_BUFFER_LEN_MAX];
    if (text.empty() || !qrcodegen_encodeText(text.c_str(), tmp, qr, qrcodegen_Ecc_MEDIUM,
            qrcodegen_VERSION_MIN, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
        return;
    }
    int count = qrcodegen_getSize(qr);
    const int quiet = 2;
    float module = sizePx / (float)(count + quiet * 2);
    float total = module * (count + quiet * 2);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float startX = origin.x + (ImGui::GetContentRegionAvail().x - total) * 0.5f;
    float startY = origin.y;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(startX, startY), ImVec2(startX + total, startY + total), IM_COL32(255, 255, 255, 255));
    for (int y = 0; y < count; ++y) {
        for (int x = 0; x < count; ++x) {
            if (qrcodegen_getModule(qr, x, y)) {
                float px = startX + (x + quiet) * module;
                float py = startY + (y + quiet) * module;
                dl->AddRectFilled(ImVec2(px, py), ImVec2(px + module, py + module), IM_COL32(0, 0, 0, 255));
            }
        }
    }
    ImGui::Dummy(ImVec2(total, total));
}

int main(int argc, char** argv) {
    atexit(CleanupTempFiles);

    char myPath[MAX_PATH];
    GetModuleFileNameA(NULL, myPath, MAX_PATH);

    std::string oldExePath = std::string(myPath) + ".old";
    if (GetFileAttributesA(oldExePath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        DeleteFileA(oldExePath.c_str());
    }
    CheckForUpdates();

    std::string currentExe(myPath);

    size_t lastSlash = currentExe.find_last_of("\\/");
    std::string currentDir = currentExe.substr(0, lastSlash);
    std::string configFile = currentDir + "\\" + CONFIG_FILENAME;

    if (GetFileAttributesA(configFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
        std::string gameTitle = "Game";
        std::ifstream f(configFile);
        if (f.is_open()) {
            std::getline(f, gameTitle);
            f.close();
        }
        RunAsFakeGame(gameTitle);
        return 0;
    }

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"KAYZEQuestCompleter", nullptr };
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    wc.hIconSm = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON1));
    ::RegisterClassExW(&wc);

    int w = 550, h = 850;
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"KAYZE Discord Quest Completer", WS_POPUP, x, y, w, h, nullptr, nullptr, wc.hInstance, nullptr);
    MARGINS margins = { -1 }; DwmExtendFrameIntoClientArea(hwnd, &margins);

    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); return 1; }
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    SetupKayzeStyle();

    ImFontConfig fontConfig; fontConfig.FontDataOwnedByAtlas = false;
    ImFont* fontRegular = io.Fonts->AddFontFromMemoryTTF((void*)font_regular_data, sizeof(font_regular_data), 20.0f, &fontConfig);
    ImFont* fontBold = io.Fonts->AddFontFromMemoryTTF((void*)font_bold_data, sizeof(font_bold_data), 20.0f, &fontConfig);
    if (!fontRegular) fontRegular = io.Fonts->AddFontDefault();
    if (!fontBold) fontBold = io.Fonts->AddFontDefault();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    auto db = FetchGameDatabase();
    std::vector<RunningGame> activeGames;
    static char searchBuf[128] = "";
    static char tokenBuf[256] = "";
    GameInfo* currentSelectedGame = nullptr;

    std::string discordToken = LoadDiscordToken();
    std::string questError;
    std::vector<QuestInfo> quests;
    std::deque<GameInfo> questOnlyGames;
    bool showTokenInput = discordToken.empty();
    if (!discordToken.empty()) quests = FetchActiveQuests(discordToken, db, questOnlyGames, questError);
    bool done = false;

    while (!done) {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg); ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ImGuiStyle& style = ImGui::GetStyle();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Main", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        g_CanDrag = !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(ImVec2(0, 0), ImVec2((float)w, (float)h), IM_COL32(18, 18, 18, 255), 12.0f);

        ImGui::PushFont(fontBold);
        ImGui::AlignTextToFramePadding();
        std::string verText = CURRENT_VERSION + " Stable";
        ImGui::TextColored(ImVec4(1, 1, 1, 0.5f), "%s", verText.c_str());
        ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 1, 1, 0.5f), "|"); ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.15f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.25f));
        if (ImGui::Button("GITHUB")) ShellExecuteA(NULL, "open", "https://github.com/kayzedevx0", NULL, NULL, SW_SHOWNORMAL);
        ImGui::PopStyleColor(2); ImGui::PopFont();

        float smallBtnSize = 18.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - (smallBtnSize * 2) - 30.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.1f));
        if (ImGui::Button("-", ImVec2(smallBtnSize, smallBtnSize))) ShowWindow(hwnd, SW_MINIMIZE);
        ImGui::SameLine();
        if (ImGui::Button("X", ImVec2(smallBtnSize, smallBtnSize))) done = true;
        ImGui::PopStyleColor(); ImGui::PopStyleVar(); ImGui::Separator();

        ImGui::Spacing();
        float winWidth = ImGui::GetWindowWidth();
        ImGui::PushFont(fontBold); ImGui::SetWindowFontScale(3.5f);
        std::string title = "KAYZE";
        float titleW = ImGui::CalcTextSize(title.c_str()).x;
        ImGui::SetCursorPosX((winWidth - titleW) * 0.5f);
        ImGui::TextColored(colCyan, title.c_str());
        ImGui::SetWindowFontScale(1.0f); ImGui::PopFont();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 25.0f);

        ImGui::PushFont(fontRegular); ImGui::SetWindowFontScale(1.2f);
        std::string sub = "DISCORD QUEST COMPLETER";
        float subW = ImGui::CalcTextSize(sub.c_str()).x;
        ImGui::SetCursorPosX((winWidth - subW) * 0.5f);
        ImGui::TextColored(colTextGrey, sub.c_str());
        ImGui::SetWindowFontScale(1.0f); ImGui::PopFont();

        ImGui::Spacing(); ImGui::Spacing();

        const float tabContentHeight = 264.0f;
        ImGui::PushFont(fontBold);
        if (ImGui::BeginTabBar("MainTabs")) {
            if (ImGui::BeginTabItem("ACTIVE QUESTS")) {
                ImGui::PopFont();
                float tabStartY = ImGui::GetCursorPosY();

                ImGui::PushFont(fontBold); ImGui::AlignTextToFramePadding();
                ImGui::TextColored(ImVec4(1, 1, 1, 0.8f), "Your Discord quests:");
                float smallW = 90.0f;
                ImGui::SameLine(ImGui::GetWindowWidth() - smallW * 2 - style.ItemSpacing.x - 15.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.15f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.25f));
                ImGui::BeginDisabled(discordToken.empty());
                if (ImGui::Button("REFRESH", ImVec2(smallW, 0))) quests = FetchActiveQuests(discordToken, db, questOnlyGames, questError);
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("TOKEN", ImVec2(smallW, 0))) showTokenInput = !showTokenInput;
                ImGui::PopStyleColor(2); ImGui::PopFont();

                float questBoxHeight = tabContentHeight - (ImGui::GetCursorPosY() - tabStartY);
                ImGui::BeginChild("QuestsBox", ImVec2(0, questBoxHeight), true);
                if (showTokenInput || discordToken.empty()) {
                    static RemoteAuthSession qrAuth;
                    int qrStatus = qrAuth.GetStatus();

                    // Pick up a completed QR login.
                    if (qrStatus == RemoteAuthSession::Success) {
                        std::string tok = qrAuth.TakeToken();
                        if (!tok.empty()) {
                            discordToken = tok;
                            SaveDiscordToken(discordToken);
                            quests = FetchActiveQuests(discordToken, db, questOnlyGames, questError);
                            showTokenInput = false;
                        }
                    }

                    bool qrActive = qrStatus == RemoteAuthSession::Connecting ||
                                    qrStatus == RemoteAuthSession::WaitingForScan ||
                                    qrStatus == RemoteAuthSession::Scanned;

                    if (qrActive) {
                        ImGui::PushFont(fontRegular);
                        if (qrStatus == RemoteAuthSession::WaitingForScan) {
                            TextCenteredLine("Open Discord on your phone and scan:");
                            ImGui::Spacing();
                            DrawQrCentered(qrAuth.GetQrUrl(), 150.0f);
                        }
                        else if (qrStatus == RemoteAuthSession::Scanned) {
                            ImGui::Spacing();
                            TextCenteredLine("Logging in as " + qrAuth.GetUserDisplay());
                            TextCenteredLine("Confirm on your phone...");
                        }
                        else {
                            ImGui::Spacing();
                            TextCenteredLine("Connecting to Discord...");
                        }
                        ImGui::PopFont();
                        ImGui::Spacing();
                        ImGui::PushFont(fontBold);
                        ImGui::PushStyleColor(ImGuiCol_Button, colBtnStop);
                        if (ImGui::Button("CANCEL", ImVec2(ImGui::GetContentRegionAvail().x, 0))) qrAuth.Cancel();
                        ImGui::PopStyleColor(); ImGui::PopFont();
                    }
                    else {
                        ImGui::PushFont(fontBold);
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.72f, 0.83f, 0.5f));
                        if (ImGui::Button("LOG IN WITH QR CODE", ImVec2(ImGui::GetContentRegionAvail().x, 34))) {
                            questError.clear();
                            qrAuth.Start();
                        }
                        ImGui::PopStyleColor(); ImGui::PopFont();

                        ImGui::PushFont(fontRegular);
                        ImGui::PushStyleColor(ImGuiCol_Text, colTextGrey);
                        ImGui::TextWrapped("Scan a QR code with the Discord mobile app, or paste your token below. It is stored encrypted (Windows DPAPI) on this PC and is sent only to discord.com. Never share it with anyone.");
                        ImGui::PopStyleColor();
                        float loadW = 90.0f;
                        ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x - loadW - style.ItemSpacing.x);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 8));
                        ImGui::InputTextWithHint("##token", "Discord token...", tokenBuf, IM_ARRAYSIZE(tokenBuf), ImGuiInputTextFlags_Password);
                        ImGui::PopStyleVar(); ImGui::PopItemWidth(); ImGui::PopFont();
                        ImGui::SameLine();
                        ImGui::PushFont(fontBold);
                        if (ImGui::Button("LOAD", ImVec2(loadW, ImGui::GetFrameHeight()))) {
                            std::string newToken = TrimToken(tokenBuf);
                            if (!newToken.empty()) {
                                quests = FetchActiveQuests(newToken, db, questOnlyGames, questError);
                                if (questError.empty()) {
                                    discordToken = newToken;
                                    SaveDiscordToken(discordToken);
                                    showTokenInput = false;
                                }
                            }
                            SecureZeroMemory(tokenBuf, sizeof(tokenBuf));
                        }
                        if (!discordToken.empty()) {
                            ImGui::PushStyleColor(ImGuiCol_Button, colBtnStop);
                            if (ImGui::Button("FORGET SAVED TOKEN", ImVec2(ImGui::GetContentRegionAvail().x, 0))) {
                                ForgetDiscordToken(); discordToken.clear(); quests.clear(); questError.clear();
                            }
                            ImGui::PopStyleColor();
                        }
                        ImGui::PopFont();
                        std::string qrErr = qrStatus == RemoteAuthSession::Failed ? qrAuth.GetError() : "";
                        if (!qrErr.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", qrErr.c_str());
                        else if (!questError.empty()) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", questError.c_str());
                    }
                }
                else if (!questError.empty()) { TextCentered(questError); }
                else if (quests.empty()) { TextCentered("No active quests right now."); }
                else {
                    for (auto& quest : quests) {
                        const float rowH = 62.0f;
                        GameInfo* questGame = quest.game;
                        bool launchable = questGame != nullptr && !questGame->executables.empty() && IsDesktopQuest(quest);

                        ImGui::BeginChild(("quest_" + quest.id).c_str(), ImVec2(0, rowH), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                        float btnH = 30.0f; float btnW = 80.0f;
                        float textMaxW = ImGui::GetWindowWidth() - 15.0f - (launchable ? btnW * 2 + style.ItemSpacing.x + 25.0f : 15.0f);

                        ImGui::SetCursorPos(ImVec2(15.0f, 8.0f));
                        ImGui::PushFont(fontBold); ImGui::Text("%s", FitText(quest.gameName, textMaxW).c_str()); ImGui::PopFont();

                        std::string details = QuestTaskLabel(quest.taskType);
                        if (quest.targetSeconds > 0) details += "  " + std::to_string(quest.progressSeconds / 60) + "/" + std::to_string(quest.targetSeconds / 60) + " min";
                        if (!quest.enrolled) details += "  (not accepted yet)";
                        else if (questGame == nullptr && IsDesktopQuest(quest)) details += "  (game not found)";
                        else if (quest.guessedExe) details += "  (exe guessed)";
                        ImGui::SetCursorPos(ImVec2(15.0f, 33.0f));
                        ImGui::PushFont(fontRegular); ImGui::TextColored(colTextGrey, "%s", FitText(details, textMaxW).c_str()); ImGui::PopFont();

                        if (launchable) {
                            std::vector<RunningGame>::iterator runningIt = activeGames.end();
                            for (auto it = activeGames.begin(); it != activeGames.end(); ++it) {
                                if (it->info.id == questGame->id && it->isRunning) { runningIt = it; break; }
                            }
                            ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - btnW * 2 - style.ItemSpacing.x - 15.0f, (rowH - btnH) * 0.5f));
                            ImGui::PushFont(fontBold);
                            if (runningIt != activeGames.end()) {
                                ImGui::PushStyleColor(ImGuiCol_Button, colBtnStop);
                                if (ImGui::Button("STOP", ImVec2(btnW, btnH))) { StopGhostProcess(*runningIt); activeGames.erase(runningIt); }
                                ImGui::PopStyleColor();
                            }
                            else {
                                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.72f, 0.83f, 0.5f));
                                if (ImGui::Button("START", ImVec2(btnW, btnH))) {
                                    RunningGame rg; rg.info = *questGame;
                                    if (StartGhostProcess(rg, questGame->executables[0], questGame->executablePaths[0])) activeGames.push_back(rg);
                                    currentSelectedGame = questGame;
                                }
                                ImGui::PopStyleColor();
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("SELECT", ImVec2(btnW, btnH))) currentSelectedGame = questGame;
                            ImGui::PopFont();
                        }
                        ImGui::EndChild();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
                ImGui::PushFont(fontBold);
            }
            if (ImGui::BeginTabItem("SEARCH")) {
                ImGui::PopFont();
            ImGui::PushFont(fontRegular); ImGui::SetCursorPosX(30);
            ImGui::PushItemWidth(winWidth - 60);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 10));
            ImGui::InputTextWithHint("##search", "Search game...", searchBuf, IM_ARRAYSIZE(searchBuf));
            ImGui::PopStyleVar(); ImGui::PopItemWidth(); ImGui::PopFont(); ImGui::Spacing();

            ImGui::PushFont(fontBold); ImGui::TextColored(ImVec4(1, 1, 1, 0.8f), "Search Results:"); ImGui::PopFont();

            ImGui::BeginChild("ResultsBox", ImVec2(0, 180), true);
            std::string searchQ = ToLower(searchBuf);

            if (searchQ.length() > 0) {
                bool found = false;
                for (auto& game : db) {
                    if (ToLower(game.name).find(searchQ) != std::string::npos) {
                        found = true;
                        ImGui::BeginChild(game.name.c_str(), ImVec2(0, 55), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
                        float textH = ImGui::GetTextLineHeight();
                        ImGui::SetCursorPosY((55.0f - textH) * 0.5f); ImGui::SetCursorPosX(15.0f);
                        ImGui::PushFont(fontBold); ImGui::Text("%s", game.name.c_str()); ImGui::PopFont();
                        float btnH = 30.0f; float btnW = 90.0f;
                        ImGui::SetCursorPosY((55.0f - btnH) * 0.5f); ImGui::SetCursorPosX(ImGui::GetWindowWidth() - btnW - 15.0f);
                        ImGui::PushFont(fontBold);
                        if (ImGui::Button("SELECT", ImVec2(btnW, btnH))) currentSelectedGame = &game;
                        ImGui::PopFont(); ImGui::EndChild();
                    }
                }
                if (!found) TextCentered("No results found.");
            }
            else { TextCentered("Type a game name above..."); }
            ImGui::EndChild();
                ImGui::EndTabItem();
                ImGui::PushFont(fontBold);
            }
            ImGui::EndTabBar();
        }
        ImGui::PopFont();

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        float footerHeight = 40.0f; float bottomMargin = 10.0f;
        if (currentSelectedGame != nullptr) {
            ImGui::PushFont(fontBold); ImGui::TextColored(colCyan, "SELECTED GAME:"); ImGui::PopFont();
            float boxHeight = ImGui::GetContentRegionAvail().y - footerHeight - bottomMargin;
            ImGui::BeginChild("SelectionBox", ImVec2(0, boxHeight), true);
            ImGui::PushFont(fontBold); ImGui::SetWindowFontScale(1.2f);
            ImGui::Text("%s", currentSelectedGame->name.c_str()); ImGui::SetWindowFontScale(1.0f); ImGui::PopFont();
            ImGui::PushFont(fontRegular); ImGui::TextDisabled("ID: %s", currentSelectedGame->id.c_str());
            ImGui::Separator(); ImGui::Spacing(); ImGui::Text("Available versions:"); ImGui::PopFont();
            float availWidth = ImGui::GetContentRegionAvail().x;
            int numExecutables = (int)currentSelectedGame->executables.size();
            float spacing = style.ItemSpacing.x; float halfWidth = (availWidth - spacing) * 0.5f;

            ImGui::PushFont(fontBold);
            for (int i = 0; i < numExecutables; ++i) {
                const auto& exe = currentSelectedGame->executables[i];
                const auto& exePath = currentSelectedGame->executablePaths[i];
                bool alreadyRunning = false;
                std::vector<RunningGame>::iterator activeGameIt;
                for (auto it = activeGames.begin(); it != activeGames.end(); ++it) {
                    if (it->activeExePath.find(exe) != std::string::npos && it->isRunning) {
                        alreadyRunning = true; activeGameIt = it; break;
                    }
                }
                float currentBtnWidth = halfWidth;
                if (numExecutables % 2 != 0 && i == numExecutables - 1) currentBtnWidth = availWidth;

                if (alreadyRunning) {
                    ImGui::PushStyleColor(ImGuiCol_Button, colBtnStop);
                    if (ImGui::Button(("STOP: " + exe).c_str(), ImVec2(currentBtnWidth, 30))) {
                        StopGhostProcess(*activeGameIt); activeGames.erase(activeGameIt);
                    }
                    ImGui::PopStyleColor();
                }
                else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.72f, 0.83f, 0.5f));
                    if (ImGui::Button(("START: " + exe).c_str(), ImVec2(currentBtnWidth, 30))) {
                        RunningGame rg; rg.info = *currentSelectedGame;
                        if (StartGhostProcess(rg, exe, exePath)) activeGames.push_back(rg);
                    }
                    ImGui::PopStyleColor();
                }
                if (i % 2 == 0 && i < numExecutables - 1) ImGui::SameLine();
            }
            ImGui::PopFont(); ImGui::EndChild();
        }
        else {
            float boxHeight = ImGui::GetContentRegionAvail().y - footerHeight - bottomMargin;
            ImGui::BeginChild("SelectionBox", ImVec2(0, boxHeight), true);
            TextCentered("No game selected."); ImGui::EndChild();
        }

        float windowHeight = ImGui::GetWindowHeight(); float footerStartY = windowHeight - 40.0f;
        ImGui::SetCursorPosY(footerStartY); ImGui::Separator();
        float textLineHeight = ImGui::GetTextLineHeight(); float textCenterY = footerStartY + (40.0f - textLineHeight) * 0.5f;
        std::string t1 = "dev by "; std::string t2 = "KAYZE";
        float totalW = ImGui::CalcTextSize(t1.c_str()).x + ImGui::CalcTextSize(t2.c_str()).x;
        float textX = (ImGui::GetWindowWidth() - totalW) * 0.5f;
        ImGui::SetCursorPos(ImVec2(textX, textCenterY));
        ImGui::PushFont(fontRegular); ImGui::TextDisabled("%s", t1.c_str()); ImGui::PopFont();
        ImGui::SameLine(0, 0);
        ImGui::PushFont(fontBold); ImGui::TextColored(colCyan, "%s", t2.c_str()); ImGui::PopFont();

        ImGui::End(); ImGui::Render();
        const float clear_color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    for (auto& g : activeGames) StopGhostProcess(g);
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext();
    CleanupDeviceD3D(); ::DestroyWindow(hwnd); ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Width = 0; sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd; sd.SampleDesc.Count = 1; sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    UINT createDeviceFlags = 0; D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget(); return true;
}
void CleanupDeviceD3D() { CleanupRenderTarget(); if (g_pSwapChain) g_pSwapChain->Release(); if (g_pd3dDeviceContext) g_pd3dDeviceContext->Release(); if (g_pd3dDevice) g_pd3dDevice->Release(); }
void CreateRenderTarget() { ID3D11Texture2D* pBackBuffer; g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer)); g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView); pBackBuffer->Release(); }
void CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; } }
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_SIZE: if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) { CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget(); } return 0;
    case WM_NCHITTEST: { LRESULT hit = DefWindowProc(hWnd, msg, wParam, lParam); if (hit == HTCLIENT && g_CanDrag) return HTCAPTION; return hit; }
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    } return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}