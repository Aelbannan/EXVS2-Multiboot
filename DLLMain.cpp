#pragma warning( disable : 6031 )
#include <winsock2.h>
#include <stdarg.h>
#include <stdio.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include "DLLMain.h"
#include "MinHook.h"
#include <thread>

#pragma comment(lib, "ws2_32.lib")

// --- Function Typedefs for Original Functions ---
typedef int (WSAAPI* BIND_PROC)(SOCKET s, const struct sockaddr* name, int namelen);
typedef int (WSAAPI* SENDTO_PROC)(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen);
typedef int (WSAAPI* RECVFROM_PROC)(SOCKET s, char* buf, int len, int flags, struct sockaddr* from, int* fromlen);

// --- Pointers to Original Functions (Trampolines) ---
BIND_PROC pOriginalBind = NULL;
SENDTO_PROC pOriginalSendto = NULL;
RECVFROM_PROC pOriginalRecvfrom = nullptr;

bool g_killerThreadStarted = false;
SOCKET g_broadcastSocket = INVALID_SOCKET;

int g_sharedPort = -1;
int g_instanceNumber = -1;
int g_cardServerPort = -1;

void DebugPrint(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);

    // Format the string into our buffer
    vsnprintf(buffer, sizeof(buffer), format, args);

    // Send to the system debugger
    OutputDebugStringA(buffer);

    va_end(args);
}

int get_port(const struct sockaddr* sa) {
    // Identify if IPv4 (AF_INET) or IPv6 (AF_INET6)
    if (sa->sa_family == AF_INET) {
        struct sockaddr_in* addr4 = (struct sockaddr_in*)sa;
        return ntohs(addr4->sin_port); // Convert Network -> Host
    }
    else if (sa->sa_family == AF_INET6) {
        struct sockaddr_in6* addr6 = (struct sockaddr_in6*)sa;
        return ntohs(addr6->sin6_port);
    }

    return -1;
}

void SocketKillerThread() {
    DebugPrint("[EXVS2] Killer started: %d\n", g_broadcastSocket);
    SOCKET s = g_broadcastSocket;
    if (s == INVALID_SOCKET) return;

    // Wait for 10 seconds without blocking the game
    Sleep(1000);

    // Shutdown the connection first (cleaner than just closing)
    shutdown(s, SD_BOTH);

    struct linger so_linger;
    so_linger.l_onoff = 1;  // Enable linger
    so_linger.l_linger = 0; // Set timeout to 0 (Hard Reset
    setsockopt(s, SOL_SOCKET, SO_LINGER, (const char*)&so_linger, sizeof(so_linger));

    // Close it - this will trigger an error in the game's recv/send logic
    if (closesocket(s) == 0) {
        DebugPrint("[EXVS2] Socket killed: %d\n", g_broadcastSocket);
        g_broadcastSocket = INVALID_SOCKET;

    }
}

// --- Detour Functions (Our Hooks) ---

// Hook for 'bind' (for server/listener ports)
int WSAAPI DetourBind(SOCKET s, const struct sockaddr* name, int namelen) {
    struct sockaddr_in* addr = (struct sockaddr_in*)name;
    int port = get_port(name);
    DebugPrint("[EXVS2] Bind %d", get_port(name));

    // If broadcast socket, save it
    if (port == g_sharedPort) {
        g_broadcastSocket = s;
    }

    return pOriginalBind(s, name, namelen);
}

// Once instances starts sending to the server, close the other instance
int WSAAPI DetourSendto(SOCKET s, const char* buf, int len, int flags, const struct sockaddr* to, int tolen) {
    if (g_killerThreadStarted == false) {
        if (to != NULL && to->sa_family == AF_INET) {
            struct sockaddr_in* addr = (struct sockaddr_in*)to;

            int port = ntohs(addr->sin_port);
            if (port != g_sharedPort && port != g_cardServerPort) {
                g_killerThreadStarted = true;
                std::thread(SocketKillerThread).detach();
            }
        }
    }
    return pOriginalSendto(s, buf, len, flags, to, tolen);
}

void LoadConfig() {
    char iniPath[MAX_PATH];

    // 1. Get the path to the directory where the game is running
    GetModuleFileNameA(NULL, iniPath, MAX_PATH);

    // 2. Remove the "game.exe" part and replace it with your ini name
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) {
        strcpy(lastSlash + 1, "net.ini");
    }

    // Now iniPath is something like "C:\Arcade\Game\config.ini"
    DebugPrint("Loading %s\n", iniPath);

    g_sharedPort = GetPrivateProfileIntA("General", "SharedPort", -1, iniPath);
    g_instanceNumber =GetPrivateProfileIntA("General", "InstanceNumber", -1, iniPath);
    g_cardServerPort = GetPrivateProfileIntA("General", "CardServerPort", -1, iniPath);
}


VOID Initialise(PVOID lpParameter)
{
    LoadConfig();

    if (g_sharedPort == -1 || g_instanceNumber == -1 || g_cardServerPort == -1) {
        DebugPrint("[EXVS2 OB] net.ini needs SharedPort, InstanceNumber, CardServerPort entries. SP: %d, IN: %d, CSP: %d", g_sharedPort, g_instanceNumber, g_cardServerPort);
    }

    if (MH_Initialize() != MH_OK) {
        DebugPrint("[EXVS2 OB] Couldn't initialize MinHook \n");
        return;
    }

    // Create hooks for the target APIs in ws2_32.dll
    MH_CreateHookApi(L"ws2_32.dll", "bind", DetourBind, reinterpret_cast<LPVOID*>(&pOriginalBind));
    MH_CreateHookApi(L"ws2_32.dll", "sendto", DetourSendto, reinterpret_cast<LPVOID*>(&pOriginalSendto));


    // Enable all created hooks
    MH_EnableHook(MH_ALL_HOOKS);
}


BOOL WINAPI DllMain(HMODULE hModule,
                    DWORD  ul_reason_for_call,
                    LPVOID lpReserved
                   )
{
    switch (ul_reason_for_call)
    {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);
            CreateThread(NULL, NULL, reinterpret_cast<LPTHREAD_START_ROUTINE>(Initialise), (PVOID)hModule, NULL, NULL);
            break;
        }
        case DLL_PROCESS_DETACH:
        {
            break;
        }
    }

    return TRUE;
}