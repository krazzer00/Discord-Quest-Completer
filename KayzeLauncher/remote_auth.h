// Discord "login with QR code" (remote auth) support.
//
// Implements the desktop remote-auth gateway flow so the user can sign in by
// scanning a QR code with the Discord mobile app instead of pasting a token.
// The whole exchange runs on a background thread; the UI polls RemoteAuthState.
#pragma once

#include <string>
#include <atomic>
#include <mutex>
#include <thread>

class RemoteAuthSession {
public:
    enum Status {
        Idle,            // not started
        Connecting,      // opening the gateway, doing the handshake
        WaitingForScan,  // QR is ready, waiting for the phone to scan it
        Scanned,         // phone scanned it, waiting for the user to approve
        Success,         // token captured
        Failed,          // something went wrong (see error)
        Canceled         // user canceled on the phone or in the app
    };

    RemoteAuthSession() = default;
    ~RemoteAuthSession();
    RemoteAuthSession(const RemoteAuthSession&) = delete;
    RemoteAuthSession& operator=(const RemoteAuthSession&) = delete;

    void Start();           // begins the flow on a background thread
    void Cancel();          // asks the worker to stop; blocks until it does
    int GetStatus() const { return m_status.load(); }

    // Snapshot of the strings guarded by the internal mutex.
    std::string GetQrUrl();
    std::string GetUserDisplay();
    std::string GetError();
    std::string TakeToken();  // returns the token once, clearing it

private:
    void Run();
    void SetStatus(Status s) { m_status.store(s); }
    void SetError(const std::string& e);

    std::thread m_worker;
    std::atomic<int> m_status{ Idle };
    std::atomic<bool> m_stop{ false };

    std::mutex m_mtx;
    std::string m_qrUrl;
    std::string m_userDisplay;
    std::string m_token;
    std::string m_error;

    // Lets Cancel() abort a worker that is blocked in a WebSocket receive.
    // Holds a WebSocketClient* (opaque here; it's file-local in the .cpp).
    std::mutex m_wsMtx;
    void* m_wsOpaque = nullptr;
};
