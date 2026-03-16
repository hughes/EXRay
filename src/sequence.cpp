// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef UNICODE
#define UNICODE
#endif

#include "sequence.h"

#include <algorithm>

int SequenceInfo::FindNearest(int frameNumber) const
{
    if (frames.empty())
        return -1;

    // Binary search for exact match or nearest
    int lo = 0;
    int hi = static_cast<int>(frames.size()) - 1;

    // Exact match shortcut
    while (lo <= hi)
    {
        int mid = (lo + hi) / 2;
        if (frames[mid].frameNumber == frameNumber)
            return mid;
        if (frames[mid].frameNumber < frameNumber)
            lo = mid + 1;
        else
            hi = mid - 1;
    }

    // lo is the insertion point — nearest is either lo or lo-1
    if (lo >= static_cast<int>(frames.size()))
        return static_cast<int>(frames.size()) - 1;
    if (lo == 0)
        return 0;

    int diffLo = frames[lo].frameNumber - frameNumber;
    int diffHi = frameNumber - frames[lo - 1].frameNumber;
    return (diffHi <= diffLo) ? lo - 1 : lo;
}

// Extract the directory and filename from a full path.
static void SplitPath(const std::wstring& fullPath, std::wstring& dir, std::wstring& filename)
{
    size_t lastSep = fullPath.find_last_of(L"\\/");
    if (lastSep != std::wstring::npos)
    {
        dir = fullPath.substr(0, lastSep + 1); // includes trailing slash
        filename = fullPath.substr(lastSep + 1);
    }
    else
    {
        dir.clear();
        filename = fullPath;
    }
}

// Find the last group of consecutive digits in the filename (before the extension).
// Returns the start index and length of the digit group within the filename.
// Returns false if no digit group is found.
static bool FindFrameNumberInFilename(const std::wstring& filename, size_t& digitStart, size_t& digitLen)
{
    // Find extension
    size_t dotPos = filename.find_last_of(L'.');
    if (dotPos == std::wstring::npos)
        dotPos = filename.size();

    // Search backwards from the extension for the last group of digits
    size_t end = dotPos;
    while (end > 0 && filename[end - 1] >= L'0' && filename[end - 1] <= L'9')
        end--;

    if (end == dotPos)
        return false; // no digits found before extension

    digitStart = end;
    digitLen = dotPos - end;
    return true;
}

// Parse frame number from a digit string (handles leading zeros).
static int ParseFrameNumber(const std::wstring& filename, size_t digitStart, size_t digitLen)
{
    int num = 0;
    for (size_t i = 0; i < digitLen; i++)
        num = num * 10 + (filename[digitStart + i] - L'0');
    return num;
}

SequenceInfo DetectSequence(const std::wstring& filePath)
{
    SequenceInfo result;

    std::wstring dir, filename;
    SplitPath(filePath, dir, filename);

    // Find the frame number digits in the filename
    size_t digitStart, digitLen;
    if (!FindFrameNumberInFilename(filename, digitStart, digitLen))
        return result; // no numeric component — not a sequence

    // Extract prefix and suffix
    // e.g. "render.0042.exr" → prefix="render.", suffix=".exr"
    std::wstring prefix = filename.substr(0, digitStart);
    std::wstring suffix = filename.substr(digitStart + digitLen);

    result.prefix = dir + prefix;
    result.suffix = suffix;

    // Scan directory for matching files
    std::wstring searchPattern = dir + prefix + L"*" + suffix;
    WIN32_FIND_DATAW findData;
    HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &findData);
    if (hFind == INVALID_HANDLE_VALUE)
        return result;

    int targetFrameNumber = ParseFrameNumber(filename, digitStart, digitLen);

    do
    {
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        std::wstring foundName = findData.cFileName;

        // Verify this file matches the exact prefix and suffix
        if (foundName.size() < prefix.size() + suffix.size())
            continue;
        if (_wcsnicmp(foundName.c_str(), prefix.c_str(), prefix.size()) != 0)
            continue;
        if (_wcsicmp(foundName.c_str() + foundName.size() - suffix.size(), suffix.c_str()) != 0)
            continue;

        // Extract and validate the digit portion
        size_t foundDigitStart = prefix.size();
        size_t foundDigitLen = foundName.size() - prefix.size() - suffix.size();
        if (foundDigitLen == 0)
            continue;

        bool allDigits = true;
        for (size_t i = 0; i < foundDigitLen; i++)
        {
            if (foundName[foundDigitStart + i] < L'0' || foundName[foundDigitStart + i] > L'9')
            {
                allDigits = false;
                break;
            }
        }
        if (!allDigits)
            continue;

        int frameNum = ParseFrameNumber(foundName, foundDigitStart, foundDigitLen);
        result.frames.push_back({frameNum, dir + foundName});
    } while (FindNextFileW(hFind, &findData));

    FindClose(hFind);

    // Sort by frame number
    std::sort(result.frames.begin(), result.frames.end(),
              [](const SequenceFrame& a, const SequenceFrame& b) { return a.frameNumber < b.frameNumber; });

    // Only treat as a sequence if there are multiple frames
    if (result.frames.size() <= 1)
    {
        result.frames.clear();
        return result;
    }

    // Find the current frame
    result.currentIndex = result.FindNearest(targetFrameNumber);

    return result;
}
