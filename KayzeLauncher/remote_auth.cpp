#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#include "remote_auth.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <vector>
#include <cstdint>

#include "json.hpp"
using json = nlohmann::json;

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace {

const wchar_t* GATEWAY_HOST = L"remote-auth-gateway.discord.gg";
const wchar_t* GATEWAY_PATH = L"/?v=2";
const wchar_t* API_HOST = L"discord.com";
const wchar_t* LOGIN_PATH = L"/api/v9/users/@me/remote-auth/login";

typedef std::vector<uint8_t> Bytes;

// --- base64 helpers -------------------------------------------------------

std::string Base64Encode(const Bytes& data) {
    if (data.empty()) return "";
    DWORD len = 0;
    if (!CryptBinaryToStringA(data.data(), (DWORD)data.size(),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len))
        return "";
    std::string out(len, '\0');
    if (!CryptBinaryToStringA(data.data(), (DWORD)data.size(),
            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &out[0], &len))
        return "";
    out.resize(len);
    return out;
}

Bytes Base64Decode(const std::string& text) {
    DWORD len = 0;
    if (!CryptStringToBinaryA(text.c_str(), (DWORD)text.size(),
            CRYPT_STRING_BASE64, nullptr, &len, nullptr, nullptr))
        return {};
    Bytes out(len);
    if (!CryptStringToBinaryA(text.c_str(), (DWORD)text.size(),
            CRYPT_STRING_BASE64, out.data(), &len, nullptr, nullptr))
        return {};
    out.resize(len);
    return out;
}

std::string ToBase64Url(std::string b64) {
    for (char& c : b64) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!b64.empty() && b64.back() == '=') b64.pop_back();
    return b64;
}

// --- DER helpers for building an X.509 SubjectPublicKeyInfo ----------------

void DerLength(Bytes& out, size_t len) {
    if (len < 0x80) {
        out.push_back((uint8_t)len);
    } else if (len <= 0xFF) {
        out.push_back(0x81);
        out.push_back((uint8_t)len);
    } else {
        out.push_back(0x82);
        out.push_back((uint8_t)(len >> 8));
        out.push_back((uint8_t)(len & 0xFF));
    }
}

Bytes DerTlv(uint8_t tag, const Bytes& content) {
    Bytes out;
    out.push_back(tag);
    DerLength(out, content.size());
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

// Encodes a big-endian unsigned integer as a DER INTEGER (adds a leading 0x00
// when the top bit is set so it stays positive).
Bytes DerInteger(const Bytes& beValue) {
    size_t i = 0;
    while (i + 1 < beValue.size() && beValue[i] == 0) i++;  // strip leading zeros
    Bytes v(beValue.begin() + i, beValue.end());
    if (v.empty()) v.push_back(0);
    if (v[0] & 0x80) v.insert(v.begin(), 0x00);
    return DerTlv(0x02, v);
}

// Builds SubjectPublicKeyInfo DER from an RSA modulus and exponent.
Bytes BuildSpki(const Bytes& modulus, const Bytes& exponent) {
    Bytes rsaPubKey;  // SEQUENCE { INTEGER n, INTEGER e }
    {
        Bytes seq = DerInteger(modulus);
        Bytes e = DerInteger(exponent);
        seq.insert(seq.end(), e.begin(), e.end());
        rsaPubKey = DerTlv(0x30, seq);
    }
    // AlgorithmIdentifier SEQUENCE { OID 1.2.840.113549.1.1.1, NULL }
    static const uint8_t kRsaOid[] = { 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86,
                                       0xF7, 0x0D, 0x01, 0x01, 0x01, 0x05, 0x00 };
    Bytes algId = DerTlv(0x30, Bytes(kRsaOid, kRsaOid + sizeof(kRsaOid)));

    Bytes bitStr;  // BIT STRING { 0x00 unused bits, rsaPubKey }
    bitStr.push_back(0x00);
    bitStr.insert(bitStr.end(), rsaPubKey.begin(), rsaPubKey.end());
    Bytes bitStrTlv = DerTlv(0x03, bitStr);

    Bytes spki = algId;
    spki.insert(spki.end(), bitStrTlv.begin(), bitStrTlv.end());
    return DerTlv(0x30, spki);
}

// --- RSA key / crypto wrapper ---------------------------------------------

class RsaKey {
public:
    ~RsaKey() {
        if (m_key) BCryptDestroyKey(m_key);
        if (m_alg) BCryptCloseAlgorithmProvider(m_alg, 0);
    }

    bool Generate() {
        if (BCryptOpenAlgorithmProvider(&m_alg, BCRYPT_RSA_ALGORITHM, nullptr, 0) != STATUS_SUCCESS)
            return false;
        if (BCryptGenerateKeyPair(m_alg, &m_key, 2048, 0) != STATUS_SUCCESS)
            return false;
        if (BCryptFinalizeKeyPair(m_key, 0) != STATUS_SUCCESS)
            return false;
        return true;
    }

    // Base64 of the public key as X.509 SubjectPublicKeyInfo DER.
    std::string EncodedPublicKey() {
        ULONG cb = 0;
        if (BCryptExportKey(m_key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &cb, 0) != STATUS_SUCCESS)
            return "";
        Bytes blob(cb);
        if (BCryptExportKey(m_key, nullptr, BCRYPT_RSAPUBLIC_BLOB, blob.data(), cb, &cb, 0) != STATUS_SUCCESS)
            return "";
        if (blob.size() < sizeof(BCRYPT_RSAKEY_BLOB)) return "";

        auto* h = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(blob.data());
        size_t off = sizeof(BCRYPT_RSAKEY_BLOB);
        if (off + h->cbPublicExp + h->cbModulus > blob.size()) return "";
        Bytes exponent(blob.begin() + off, blob.begin() + off + h->cbPublicExp);
        off += h->cbPublicExp;
        Bytes modulus(blob.begin() + off, blob.begin() + off + h->cbModulus);

        return Base64Encode(BuildSpki(modulus, exponent));
    }

    // RSA-OAEP (SHA-256) decrypt.
    Bytes Decrypt(const Bytes& ciphertext) {
        BCRYPT_OAEP_PADDING_INFO oaep;
        oaep.pszAlgId = BCRYPT_SHA256_ALGORITHM;
        oaep.pbLabel = nullptr;
        oaep.cbLabel = 0;
        ULONG cb = 0;
        if (BCryptDecrypt(m_key, (PUCHAR)ciphertext.data(), (ULONG)ciphertext.size(),
                &oaep, nullptr, 0, nullptr, 0, &cb, BCRYPT_PAD_OAEP) != STATUS_SUCCESS)
            return {};
        Bytes out(cb);
        if (BCryptDecrypt(m_key, (PUCHAR)ciphertext.data(), (ULONG)ciphertext.size(),
                &oaep, nullptr, 0, out.data(), cb, &cb, BCRYPT_PAD_OAEP) != STATUS_SUCCESS)
            return {};
        out.resize(cb);
        return out;
    }

private:
    BCRYPT_ALG_HANDLE m_alg = nullptr;
    BCRYPT_KEY_HANDLE m_key = nullptr;
};

Bytes Sha256(const Bytes& data) {
    Bytes digest(32);
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != STATUS_SUCCESS)
        return {};
    NTSTATUS st = BCryptHash(alg, nullptr, 0, (PUCHAR)data.data(), (ULONG)data.size(),
                             digest.data(), (ULONG)digest.size());
    BCryptCloseAlgorithmProvider(alg, 0);
    if (st != STATUS_SUCCESS) return {};
    return digest;
}

// --- WebSocket wrapper over WinHTTP ---------------------------------------

class WebSocketClient {
public:
    ~WebSocketClient() { Close(); }

    bool Connect(const wchar_t* host, const wchar_t* path) {
        m_session = WinHttpOpen(L"KayzeLauncher",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!m_session) return false;
        m_connect = WinHttpConnect(m_session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!m_connect) return false;
        HINTERNET req = WinHttpOpenRequest(m_connect, L"GET", path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!req) return false;

        if (!WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
            WinHttpCloseHandle(req); return false;
        }
        const wchar_t* origin = L"Origin: https://discord.com\r\n";
        if (!WinHttpSendRequest(req, origin, (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            WinHttpCloseHandle(req); return false;
        }
        if (!WinHttpReceiveResponse(req, nullptr)) {
            WinHttpCloseHandle(req); return false;
        }
        m_ws = WinHttpWebSocketCompleteUpgrade(req, 0);
        WinHttpCloseHandle(req);
        return m_ws != nullptr;
    }

    bool SendText(const std::string& text) {
        std::lock_guard<std::mutex> lock(m_sendMtx);
        if (!m_ws) return false;
        return WinHttpWebSocketSend(m_ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            (PVOID)text.data(), (DWORD)text.size()) == NO_ERROR;
    }

    // Receives one complete message. Returns false on close/error.
    bool ReceiveText(std::string& out) {
        out.clear();
        uint8_t buf[4096];
        for (;;) {
            DWORD read = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
            DWORD rc = WinHttpWebSocketReceive(m_ws, buf, sizeof(buf), &read, &type);
            if (rc != NO_ERROR) return false;
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) return false;
            out.append((const char*)buf, read);
            if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
                return true;  // final fragment of the message
        }
    }

    // Closes just the socket handle to unblock a thread waiting in ReceiveText.
    // Safe to call from another thread; idempotent.
    void AbortReceive() {
        std::lock_guard<std::mutex> lock(m_closeMtx);
        if (m_ws) { HINTERNET h = m_ws; m_ws = nullptr; WinHttpCloseHandle(h); }
    }

    void Close() {
        std::lock_guard<std::mutex> lock(m_closeMtx);
        if (m_ws) {
            WinHttpWebSocketClose(m_ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            WinHttpCloseHandle(m_ws);
            m_ws = nullptr;
        }
        if (m_connect) { WinHttpCloseHandle(m_connect); m_connect = nullptr; }
        if (m_session) { WinHttpCloseHandle(m_session); m_session = nullptr; }
    }

private:
    HINTERNET m_session = nullptr;
    HINTERNET m_connect = nullptr;
    HINTERNET m_ws = nullptr;
    std::mutex m_sendMtx;
    std::mutex m_closeMtx;
};

// POST {"ticket": ...} to the login endpoint and return the encrypted_token.
std::string ExchangeTicket(const std::string& ticket) {
    std::string result;
    HINTERNET session = WinHttpOpen(L"KayzeLauncher",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;
    HINTERNET connect = WinHttpConnect(session, API_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET req = connect ? WinHttpOpenRequest(connect, L"POST", LOGIN_PATH, nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (req) {
        json body = { {"ticket", ticket} };
        std::string payload = body.dump();
        const wchar_t* headers = L"Content-Type: application/json\r\n";
        if (WinHttpSendRequest(req, headers, (DWORD)-1L,
                (PVOID)payload.data(), (DWORD)payload.size(), (DWORD)payload.size(), 0) &&
            WinHttpReceiveResponse(req, nullptr)) {
            std::string resp;
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, &chunk[0], avail, &read)) break;
                resp.append(chunk.data(), read);
            }
            try {
                json data = json::parse(resp);
                if (data.contains("encrypted_token") && data["encrypted_token"].is_string())
                    result = data["encrypted_token"].get<std::string>();
            } catch (...) {}
        }
        WinHttpCloseHandle(req);
    }
    if (connect) WinHttpCloseHandle(connect);
    if (session) WinHttpCloseHandle(session);
    return result;
}

}  // namespace

// --- RemoteAuthSession -----------------------------------------------------

RemoteAuthSession::~RemoteAuthSession() {
    Cancel();
}

void RemoteAuthSession::Start() {
    Cancel();
    m_stop.store(false);
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_qrUrl.clear(); m_userDisplay.clear(); m_token.clear(); m_error.clear();
    }
    SetStatus(Connecting);
    m_worker = std::thread(&RemoteAuthSession::Run, this);
}

void RemoteAuthSession::Cancel() {
    m_stop.store(true);
    {
        // Unblock the worker if it's waiting on a WebSocket receive.
        std::lock_guard<std::mutex> lock(m_wsMtx);
        if (m_wsOpaque) static_cast<WebSocketClient*>(m_wsOpaque)->AbortReceive();
    }
    if (m_worker.joinable()) m_worker.join();
}

void RemoteAuthSession::SetError(const std::string& e) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_error = e;
}

std::string RemoteAuthSession::GetQrUrl() {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_qrUrl;
}
std::string RemoteAuthSession::GetUserDisplay() {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_userDisplay;
}
std::string RemoteAuthSession::GetError() {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_error;
}
std::string RemoteAuthSession::TakeToken() {
    std::lock_guard<std::mutex> lock(m_mtx);
    std::string t = m_token;
    m_token.clear();
    return t;
}

void RemoteAuthSession::Run() {
    RsaKey key;
    if (!key.Generate()) { SetError("Can't generate keys."); SetStatus(Failed); return; }

    std::string encodedPub = key.EncodedPublicKey();
    if (encodedPub.empty()) { SetError("Can't export key."); SetStatus(Failed); return; }

    WebSocketClient ws;
    if (!ws.Connect(GATEWAY_HOST, GATEWAY_PATH)) {
        SetError("Can't reach Discord."); SetStatus(Failed); return;
    }
    { std::lock_guard<std::mutex> lock(m_wsMtx); m_wsOpaque = &ws; }

    // Heartbeat thread: the gateway sends heartbeat_interval in the hello.
    std::atomic<int> heartbeatMs{ 0 };
    std::atomic<bool> hbStop{ false };
    std::thread heartbeat([&] {
        while (!hbStop.load() && !m_stop.load()) {
            int interval = heartbeatMs.load();
            if (interval <= 0) { Sleep(100); continue; }
            // Sleep in small slices so we notice stop/cancel quickly.
            int waited = 0;
            while (waited < interval && !hbStop.load() && !m_stop.load()) {
                Sleep(200); waited += 200;
            }
            if (hbStop.load() || m_stop.load()) break;
            ws.SendText("{\"op\":\"heartbeat\"}");
        }
    });

    bool ok = false;
    std::string finalToken;

    while (!m_stop.load()) {
        std::string msg;
        if (!ws.ReceiveText(msg)) break;

        std::string op;
        json data;
        try {
            data = json::parse(msg);
            op = data.value("op", "");
        } catch (...) { continue; }

        if (op == "hello") {
            heartbeatMs.store(data.value("heartbeat_interval", 41250));
            json init = { {"op", "init"}, {"encoded_public_key", encodedPub} };
            ws.SendText(init.dump());
        } else if (op == "nonce_proof") {
            Bytes enc = Base64Decode(data.value("encrypted_nonce", ""));
            Bytes nonce = key.Decrypt(enc);
            if (nonce.empty()) { SetError("Handshake failed."); break; }
            std::string proof = ToBase64Url(Base64Encode(Sha256(nonce)));
            json reply = { {"op", "nonce_proof"}, {"nonce", proof} };
            ws.SendText(reply.dump());
        } else if (op == "pending_remote_init") {
            std::string fp = data.value("fingerprint", "");
            if (fp.empty()) { SetError("Protocol error."); break; }
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_qrUrl = "https://discord.com/ra/" + fp;
            }
            SetStatus(WaitingForScan);
        } else if (op == "pending_ticket") {
            Bytes enc = Base64Decode(data.value("encrypted_user_payload", ""));
            Bytes payload = key.Decrypt(enc);
            std::string text((const char*)payload.data(), payload.size());
            // "id:discriminator:avatar:username"
            std::string username = text;
            size_t lastColon = text.find_last_of(':');
            if (lastColon != std::string::npos) username = text.substr(lastColon + 1);
            {
                std::lock_guard<std::mutex> lock(m_mtx);
                m_userDisplay = username;
            }
            SetStatus(Scanned);
        } else if (op == "pending_login") {
            std::string ticket = data.value("ticket", "");
            std::string encToken = ExchangeTicket(ticket);
            Bytes tok = key.Decrypt(Base64Decode(encToken));
            if (tok.empty()) { SetError("Can't decrypt token."); break; }
            finalToken.assign((const char*)tok.data(), tok.size());
            ok = true;
            break;
        } else if (op == "cancel") {
            SetStatus(Canceled);
            hbStop.store(true);
            if (heartbeat.joinable()) heartbeat.join();
            { std::lock_guard<std::mutex> lock(m_wsMtx); m_wsOpaque = nullptr; }
            ws.Close();
            return;
        }
    }

    hbStop.store(true);
    if (heartbeat.joinable()) heartbeat.join();
    { std::lock_guard<std::mutex> lock(m_wsMtx); m_wsOpaque = nullptr; }
    ws.Close();

    if (ok) {
        {
            std::lock_guard<std::mutex> lock(m_mtx);
            m_token = finalToken;
        }
        SetStatus(Success);
    } else if (m_status.load() != Canceled) {
        if (GetError().empty()) SetError(m_stop.load() ? "Canceled." : "Login timed out.");
        SetStatus(m_stop.load() ? Canceled : Failed);
    }
}
