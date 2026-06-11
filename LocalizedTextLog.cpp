#include "LocalizedTextLog.h"

#include "Utils.h"
#include "MinHook.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <unordered_map>

using LocalizedLookupHashFn = __int64(__fastcall*)(__int64* table, const char* key);
using LocalizedExpandFn = unsigned int(__fastcall*)(void* ctx, wchar_t* out, unsigned int outCap, const char* key);
using SetTextKeyFn = void(__fastcall*)(__int64** ctx, const char* key);
using AssignTextStringFn = void(__fastcall*)(void* textObj, void* srcStdString);
using BuildNuModuleStringFn = void(__fastcall*)(__int64* dst, const void* src, size_t len);

static LocalizedLookupHashFn pOriginalLookupHash = nullptr;
static LocalizedExpandFn pOriginalExpand = nullptr;
static SetTextKeyFn pOriginalSetTextKey = nullptr;
static AssignTextStringFn pOriginalAssignTextString = nullptr;
static BuildNuModuleStringFn pBuildNuModuleString = nullptr;

static constexpr size_t kNuModuleStringSize = 0x28;
static constexpr DWORD kMaxKeyLen = 512;
static constexpr DWORD kMaxWideChars = 4096;

static bool g_hooksInstalled = false;
static char g_exePath[MAX_PATH] = {};
static std::unordered_map<std::string, std::wstring> g_textOverrides;

static bool IsReadable(const void* ptr, size_t bytes);

static bool IsReadable(const void* ptr, size_t bytes) {
    if (!ptr || bytes == 0) {
        return false;
    }
    const char* p = static_cast<const char*>(ptr);
    if (Utils::IsBadReadPtrEx(const_cast<void*>(static_cast<const void*>(p)))) {
        return false;
    }
    if (bytes > 1) {
        const char* end = p + bytes - 1;
        if (Utils::IsBadReadPtrEx(const_cast<void*>(static_cast<const void*>(end)))) {
            return false;
        }
    }
    return true;
}

static bool WideToUtf8(const wchar_t* wide, std::string& out) {
    if (!wide || !IsReadable(wide, sizeof(wchar_t))) {
        return false;
    }

    size_t len = 0;
    while (len < kMaxWideChars) {
        if (!IsReadable(wide + len, sizeof(wchar_t))) {
            return false;
        }
        if (wide[len] == L'\0') {
            break;
        }
        ++len;
    }
    if (len == 0 || len >= kMaxWideChars) {
        return false;
    }

    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, wide, static_cast<int>(len), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return false;
    }

    out.resize(static_cast<size_t>(needed));
    const int written = WideCharToMultiByte(
        CP_UTF8, 0, wide, static_cast<int>(len), &out[0], needed, nullptr, nullptr);
    return written == needed;
}

static bool Utf8ToWide(const std::string& utf8, std::wstring& out) {
    if (utf8.empty()) {
        out.clear();
        return true;
    }

    const int needed = MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return false;
    }

    out.resize(static_cast<size_t>(needed));
    const int written = MultiByteToWideChar(
        CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), &out[0], needed);
    return written == needed;
}

static std::string NormalizeOverrideKey(const std::string& keyUtf8) {
    if (keyUtf8.size() > 4 && keyUtf8.compare(0, 4, "\\x10") == 0) {
        const size_t open = keyUtf8.find('[');
        const size_t close = keyUtf8.rfind(']');
        if (open != std::string::npos && close != std::string::npos && close > open) {
            return keyUtf8.substr(open + 1, close - open - 1);
        }
    }
    return keyUtf8;
}

static std::string ExtractBareKeyFromRaw(const char* key) {
    if (!key || !IsReadable(key, 1)) {
        return {};
    }

    if (static_cast<unsigned char>(key[0]) == 0x10 && key[1] == '[') {
        const char* close = strchr(key + 2, ']');
        if (close && close > key + 2) {
            return std::string(key + 2, close);
        }
    }

    const size_t len = strnlen(key, kMaxKeyLen);
    return std::string(key, len);
}

static bool ReadNuModuleStringView(const void* src, const char** outPtr, size_t* outLen) {
    if (!src || !outPtr || !outLen) {
        return false;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(src);
    const size_t size = *reinterpret_cast<const size_t*>(bytes + 0x18);
    const size_t cap = *reinterpret_cast<const size_t*>(bytes + 0x20);
    if (size == 0) {
        return false;
    }

    if (cap <= 0xf) {
        *outPtr = reinterpret_cast<const char*>(bytes + 0x08);
    } else {
        *outPtr = *reinterpret_cast<const char* const*>(bytes + 0x08);
        if (!*outPtr) {
            return false;
        }
    }

    *outLen = size;
    return true;
}

static void InitEmptyNuModuleString(void* out) {
    if (!out) {
        return;
    }

    memset(out, 0, kNuModuleStringSize);
    *reinterpret_cast<size_t*>(reinterpret_cast<uint8_t*>(out) + 0x20) = 0xf;
}

static void ReleaseNuModuleString(void* str) {
    if (!str) {
        return;
    }

    const size_t cap = *reinterpret_cast<size_t*>(reinterpret_cast<uint8_t*>(str) + 0x20);
    if (cap <= 0xf) {
        return;
    }

    void* ptr = *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(str) + 0x08);
    if (!ptr) {
        return;
    }

    using GameFreeFn = void(__fastcall*)(void*);
    static GameFreeFn pFree = nullptr;
    if (!pFree) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
        if (base) {
            pFree = reinterpret_cast<GameFreeFn>(base + 0x541E84);
        }
    }

    if (pFree) {
        pFree(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(ptr) - 8));
    }
}

static bool AssignUtf8ToNuModuleString(void* out, const std::string& utf8) {
    if (!out || utf8.empty() || !pBuildNuModuleString) {
        return false;
    }

    ReleaseNuModuleString(out);
    InitEmptyNuModuleString(out);
    pBuildNuModuleString(reinterpret_cast<__int64*>(out), utf8.data(), utf8.size());
    return true;
}

static bool HasMultipleMarkupSegments(const char* key, size_t len) {
    int markupCount = 0;
    for (size_t i = 0; i < len; ++i) {
        if (static_cast<unsigned char>(key[i]) == 0x10) {
            ++markupCount;
            if (markupCount > 1) {
                return true;
            }
        }
    }
    return false;
}

static void* GetLocalizedTextManager() {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!base) {
        return nullptr;
    }
    return reinterpret_cast<void*>(base + kRvaLocalizedTextMgr);
}

static bool DecodeJsonString(const std::string& src, size_t& pos, std::string& out) {
    if (pos >= src.size() || src[pos] != '"') {
        return false;
    }

    ++pos;
    out.clear();
    while (pos < src.size()) {
        const char ch = src[pos++];
        if (ch == '"') {
            return true;
        }
        if (ch != '\\') {
            out.push_back(ch);
            continue;
        }
        if (pos >= src.size()) {
            return false;
        }

        const char esc = src[pos++];
        switch (esc) {
        case '"':
        case '\\':
        case '/':
            out.push_back(esc);
            break;
        case 'b':
            out.push_back('\b');
            break;
        case 'f':
            out.push_back('\f');
            break;
        case 'n':
            out.push_back('\n');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'u':
            if (pos + 4 > src.size()) {
                return false;
            }
            out.append(src, pos, 4);
            pos += 4;
            break;
        default:
            out.push_back(esc);
            break;
        }
    }
    return false;
}

static void SkipJsonWhitespace(const std::string& src, size_t& pos) {
    while (pos < src.size()) {
        const char ch = src[pos];
        if (ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n') {
            break;
        }
        ++pos;
    }
}

static bool IsVariableNameChar(unsigned char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '(' || ch == ')';
}

static std::string EmbedVariableMarkupUtf8(const std::string& utf8) {
    std::string out;
    out.reserve(utf8.size() + 32);

    for (size_t i = 0; i < utf8.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(utf8[i]);
        if (ch == '[' && (i == 0 || static_cast<unsigned char>(utf8[i - 1]) != 0x10)) {
            size_t j = i + 1;
            while (j < utf8.size() && IsVariableNameChar(static_cast<unsigned char>(utf8[j]))) {
                ++j;
            }
            if (j > i + 1 && j < utf8.size() && utf8[j] == ']') {
                out.push_back(static_cast<char>(0x10));
                out.append(utf8, i, j - i + 1);
                i = j;
                continue;
            }
        }

        out.push_back(static_cast<char>(ch));
    }

    return out;
}

static bool LooksLikeMessageKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }

    for (unsigned char ch : key) {
        if (ch >= 0x80) {
            return false;
        }
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '-')) {
            return false;
        }
    }

    return true;
}

static bool LoadOverrideFile(const char* path) {
    if (!path || !path[0]) {
        return false;
    }

    FILE* file = nullptr;
    if (fopen_s(&file, path, "rb") != 0 || !file) {
        return false;
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || size > 64 * 1024 * 1024) {
        fclose(file);
        return false;
    }

    std::string content(static_cast<size_t>(size), '\0');
    const size_t read = fread(&content[0], 1, content.size(), file);
    fclose(file);
    if (read != content.size()) {
        return false;
    }

    g_textOverrides.clear();
    size_t pos = 0;
    SkipJsonWhitespace(content, pos);
    if (pos >= content.size() || content[pos] != '{') {
        return false;
    }
    ++pos;

    while (pos < content.size()) {
        SkipJsonWhitespace(content, pos);
        if (pos < content.size() && content[pos] == '}') {
            break;
        }

        std::string key;
        if (!DecodeJsonString(content, pos, key)) {
            break;
        }

        SkipJsonWhitespace(content, pos);
        if (pos >= content.size() || content[pos] != ':') {
            break;
        }
        ++pos;

        SkipJsonWhitespace(content, pos);
        std::string value;
        if (!DecodeJsonString(content, pos, value)) {
            break;
        }

        const bool messageKey = LooksLikeMessageKey(key);
        const std::string& storedValue = messageKey ? EmbedVariableMarkupUtf8(value) : value;
        std::wstring wide;
        if (Utf8ToWide(storedValue, wide)) {
            g_textOverrides[messageKey ? NormalizeOverrideKey(key) : key] = std::move(wide);
        }

        SkipJsonWhitespace(content, pos);
        if (pos < content.size() && content[pos] == ',') {
            ++pos;
        }
    }

    return !g_textOverrides.empty();
}

static const wchar_t* TryPeekOverride(const char* key) {
    if (!key || !IsReadable(key, 1) || g_textOverrides.empty()) {
        return nullptr;
    }

    std::string bare = ExtractBareKeyFromRaw(key);
    if (bare.empty()) {
        const size_t len = strnlen(key, kMaxKeyLen);
        if (len == 0) {
            return nullptr;
        }
        bare = NormalizeOverrideKey(std::string(key, len));
    }

    const auto it = g_textOverrides.find(bare);
    if (it == g_textOverrides.end()) {
        return nullptr;
    }
    return it->second.c_str();
}

static const wchar_t* TryGetOverride(const char* key) {
    return TryPeekOverride(key);
}

static const wchar_t* TryGetLiteralOverrideBounded(const char* text, size_t len) {
    if (!text || len == 0 || g_textOverrides.empty() || len >= kMaxKeyLen) {
        return nullptr;
    }

    char buf[kMaxKeyLen] = {};
    memcpy(buf, text, len);
    buf[len] = '\0';

    const auto it = g_textOverrides.find(buf);
    if (it == g_textOverrides.end()) {
        return nullptr;
    }
    return it->second.c_str();
}

static bool IsMarkupKey(const char* key, size_t len) {
    return key && len > 0 && static_cast<unsigned char>(key[0]) == 0x10;
}

static void LoadLocalizedTextConfig() {
    if (!g_exePath[0]) {
        GetModuleFileNameA(nullptr, g_exePath, MAX_PATH);
    }

    char iniPath[MAX_PATH] = {};
    strncpy_s(iniPath, g_exePath, _TRUNCATE);
    char* lastSlash = strrchr(iniPath, '\\');
    if (lastSlash) {
        strcpy_s(lastSlash + 1, MAX_PATH - (lastSlash + 1 - iniPath), "net.ini");
    }

    const int overrideEnable = GetPrivateProfileIntA("LocalizedText", "OverrideEnable", 1, iniPath);
    if (!overrideEnable) {
        return;
    }

    char overrideName[MAX_PATH] = "localized_text_overrides.json";
    GetPrivateProfileStringA(
        "LocalizedText", "OverrideFile", overrideName, overrideName, MAX_PATH, iniPath);

    char overridePath[MAX_PATH] = {};
    strncpy_s(overridePath, g_exePath, _TRUNCATE);
    char* slash = strrchr(overridePath, '\\');
    if (slash) {
        strcpy_s(slash + 1, MAX_PATH - (slash + 1 - overridePath), overrideName);
        LoadOverrideFile(overridePath);
    }
}

static __int64 __fastcall DetourLookupHash(__int64* table, const char* key) {
    if (const wchar_t* overrideText = TryGetOverride(key)) {
        return reinterpret_cast<__int64>(overrideText);
    }
    return pOriginalLookupHash(table, key);
}

static unsigned int __fastcall DetourLocalizedExpand(
    void* ctx, wchar_t* out, unsigned int outCap, const char* key) {
    if (const wchar_t* overrideText = TryGetOverride(key)) {
        if (out && outCap > 1) {
            wcsncpy_s(out, outCap, overrideText, _TRUNCATE);
            const unsigned int len = static_cast<unsigned int>(wcslen(out));
            if (len > 0) {
                return len;
            }
        }
    }
    return pOriginalExpand(ctx, out, outCap, key);
}

static bool ExpandCompositeWithOverrides(
    void* mgr, const char* key, size_t len, wchar_t* out, unsigned int outCap) {
    if (!mgr || !key || len == 0 || !out || outCap <= 1) {
        return false;
    }

    wchar_t origWide[kMaxWideChars] = {};
    const unsigned int nOrig = pOriginalExpand(mgr, origWide, kMaxWideChars, key);
    if (nOrig == 0 || origWide[0] == L'\0') {
        return false;
    }

    unsigned int outLen = 0;
    out[0] = L'\0';

    for (size_t i = 0; i < len;) {
        if (static_cast<unsigned char>(key[i]) != 0x10 || i + 2 >= len || key[i + 1] != '[') {
            return false;
        }

        const char* close = static_cast<const char*>(memchr(key + i + 2, ']', len - (i + 2)));
        if (!close) {
            return false;
        }

        const size_t segLen = static_cast<size_t>((close + 1) - (key + i));
        if (segLen == 0 || segLen >= kMaxKeyLen) {
            return false;
        }

        char seg[kMaxKeyLen] = {};
        memcpy(seg, key + i, segLen);
        seg[segLen] = '\0';

        wchar_t segWide[kMaxWideChars] = {};
        unsigned int nSeg = 0;
        if (const wchar_t* overrideText = TryPeekOverride(seg)) {
            wcsncpy_s(segWide, kMaxWideChars, overrideText, _TRUNCATE);
            nSeg = static_cast<unsigned int>(wcslen(segWide));
        } else {
            nSeg = pOriginalExpand(mgr, segWide, kMaxWideChars, seg);
        }

        if (nSeg == 0 || segWide[0] == L'\0' || outLen + nSeg >= outCap) {
            return false;
        }

        wmemcpy(out + outLen, segWide, nSeg);
        outLen += nSeg;
        out[outLen] = L'\0';
        i += segLen;
    }

    if (outLen == 0) {
        return false;
    }
    if (nOrig == outLen && wcsncmp(origWide, out, nOrig) == 0) {
        return false;
    }
    return true;
}

static bool BuildLiteralUtf8ForKey(const char* key, size_t len, std::string& utf8Out) {
    if (!key || len == 0 || len >= kMaxKeyLen) {
        return false;
    }

    char buf[kMaxKeyLen] = {};
    memcpy(buf, key, len);
    buf[len] = '\0';

    if (!IsMarkupKey(key, len)) {
        if (const wchar_t* literalText = TryGetLiteralOverrideBounded(key, len)) {
            return WideToUtf8(literalText, utf8Out) && !utf8Out.empty();
        }
        return false;
    }

    if (!HasMultipleMarkupSegments(key, len)) {
        if (const wchar_t* overrideText = TryPeekOverride(buf)) {
            return WideToUtf8(overrideText, utf8Out) && !utf8Out.empty();
        }
        return false;
    }

    void* mgr = GetLocalizedTextManager();
    wchar_t overrideWide[kMaxWideChars] = {};
    if (!ExpandCompositeWithOverrides(mgr, key, len, overrideWide, kMaxWideChars)) {
        return false;
    }

    return WideToUtf8(overrideWide, utf8Out) && !utf8Out.empty();
}

static void __fastcall DetourAssignTextString(void* textObj, void* srcStdString) {
    const char* keyPtr = nullptr;
    size_t keyLen = 0;
    if (ReadNuModuleStringView(srcStdString, &keyPtr, &keyLen)) {
        std::string utf8;
        if (BuildLiteralUtf8ForKey(keyPtr, keyLen, utf8)) {
            alignas(16) uint8_t localString[kNuModuleStringSize] = {};
            if (AssignUtf8ToNuModuleString(localString, utf8)) {
                pOriginalAssignTextString(textObj, localString);
                ReleaseNuModuleString(localString);
                return;
            }
            ReleaseNuModuleString(localString);
        }
    }

    pOriginalAssignTextString(textObj, srcStdString);
}

static void __fastcall DetourSetTextKey(__int64** ctx, const char* key) {
    if (key) {
        const size_t len = strnlen(key, kMaxKeyLen);
        std::string literalUtf8;
        if (len > 0 && BuildLiteralUtf8ForKey(key, len, literalUtf8)) {
            pOriginalSetTextKey(ctx, literalUtf8.c_str());
            return;
        }
    }

    pOriginalSetTextKey(ctx, key);
}

static bool CreateGameHook(DWORD rva, LPVOID detour, LPVOID* original) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!base) {
        return false;
    }

    LPVOID target = reinterpret_cast<LPVOID>(base + rva);
    return MH_CreateHook(target, detour, original) == MH_OK;
}

void LocalizedTextLogStartup() {
    GetModuleFileNameA(nullptr, g_exePath, MAX_PATH);
    LoadLocalizedTextConfig();
}

bool InstallLocalizedTextHooks() {
    if (g_hooksInstalled) {
        return true;
    }

    if (!g_exePath[0]) {
        LocalizedTextLogStartup();
    }

    Sleep(3000);

    bool ok = true;
    ok = CreateGameHook(
             kRvaSetTextKey,
             reinterpret_cast<LPVOID>(DetourSetTextKey),
             reinterpret_cast<LPVOID*>(&pOriginalSetTextKey)) &&
         ok;
    ok = CreateGameHook(
             kRvaAssignTextString,
             reinterpret_cast<LPVOID>(DetourAssignTextString),
             reinterpret_cast<LPVOID*>(&pOriginalAssignTextString)) &&
         ok;
    ok = CreateGameHook(
             kRvaLocalizedExpand,
             reinterpret_cast<LPVOID>(DetourLocalizedExpand),
             reinterpret_cast<LPVOID*>(&pOriginalExpand)) &&
         ok;
    ok = CreateGameHook(
             kRvaLocalizedLookupHash,
             reinterpret_cast<LPVOID>(DetourLookupHash),
             reinterpret_cast<LPVOID*>(&pOriginalLookupHash)) &&
         ok;

    const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (base) {
        pBuildNuModuleString = reinterpret_cast<BuildNuModuleStringFn>(base + kRvaBuildNuModuleString);
    }

    g_hooksInstalled = ok;
    return ok;
}

void ShutdownLocalizedTextLog() {
}
