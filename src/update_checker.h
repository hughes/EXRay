// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <cstdio>
#include <cstring>
#include <windows.h>
#include <winhttp.h>

struct UpdateCheckResult
{
    bool updateAvailable = false;
    char newVersion[32] = {};
};

namespace UpdateChecker
{

inline int PackVersion(int major, int minor, int patch) { return (major << 20) | (minor << 10) | patch; }

// Parse "tag_name":"vX.Y.Z" from GitHub API JSON response.
inline int ParseTagVersion(const char* json, char* versionOut, size_t versionOutSize)
{
    const char* key = strstr(json, "\"tag_name\"");
    if (!key)
        return 0;

    const char* colon = strchr(key + 10, ':');
    if (!colon)
        return 0;

    const char* q1 = strchr(colon, '"');
    if (!q1)
        return 0;
    q1++;

    const char* q2 = strchr(q1, '"');
    if (!q2 || q2 - q1 > 30)
        return 0;

    char buf[32];
    size_t len = static_cast<size_t>(q2 - q1);
    memcpy(buf, q1, len);
    buf[len] = '\0';

    const char* ver = buf;
    if (ver[0] == 'v' || ver[0] == 'V')
        ver++;

    int major = 0, minor = 0, patch = 0;
    if (sscanf(ver, "%d.%d.%d", &major, &minor, &patch) < 2)
        return 0;

    if (versionOut && versionOutSize > 0)
    {
        size_t verLen = strlen(ver);
        if (verLen < versionOutSize)
        {
            memcpy(versionOut, ver, verLen);
            versionOut[verLen] = '\0';
        }
    }

    return PackVersion(major, minor, patch);
}

// Synchronous HTTPS GET to GitHub releases API. Call from a background thread.
inline UpdateCheckResult Check(int currentMajor, int currentMinor, int currentPatch)
{
    UpdateCheckResult result = {};
    int currentPacked = PackVersion(currentMajor, currentMinor, currentPatch);

    HINTERNET hSession = WinHttpOpen(L"EXRay-UpdateCheck/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
        return result;

    WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/repos/hughes/EXRay/releases/latest", nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Check HTTP status
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode, &size, WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    // Read response body (cap at 32KB — actual response is ~2-3KB)
    char body[32768];
    DWORD totalRead = 0;
    DWORD bytesRead = 0;

    while (totalRead < sizeof(body) - 1)
    {
        if (!WinHttpReadData(hRequest, body + totalRead, sizeof(body) - 1 - totalRead, &bytesRead))
            break;
        if (bytesRead == 0)
            break;
        totalRead += bytesRead;
    }
    body[totalRead] = '\0';

    char remoteVersion[32] = {};
    int remotePacked = ParseTagVersion(body, remoteVersion, sizeof(remoteVersion));
    if (remotePacked > currentPacked)
    {
        result.updateAvailable = true;
        memcpy(result.newVersion, remoteVersion, sizeof(result.newVersion));
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

} // namespace UpdateChecker
