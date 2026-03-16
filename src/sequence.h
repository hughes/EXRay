// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <string>
#include <vector>
#include <windows.h>

struct SequenceFrame
{
    int frameNumber;     // parsed from filename (e.g. 42)
    std::wstring path;   // full path to this frame's file
};

struct SequenceInfo
{
    std::wstring prefix; // e.g. "C:/renders/render."
    std::wstring suffix; // e.g. ".exr"
    std::vector<SequenceFrame> frames; // sorted by frameNumber
    int currentIndex = 0; // index into frames vector

    int CurrentFrameNumber() const
    {
        if (currentIndex >= 0 && currentIndex < static_cast<int>(frames.size()))
            return frames[currentIndex].frameNumber;
        return -1;
    }

    const std::wstring& CurrentPath() const
    {
        return frames[currentIndex].path;
    }

    int FrameCount() const { return static_cast<int>(frames.size()); }

    // Find the index of the frame closest to the given frame number.
    // Returns the index into frames vector.
    int FindNearest(int frameNumber) const;
};

// Given a file path, detect if it belongs to a numbered sequence.
// Scans the directory for sibling files matching the same prefix/suffix pattern.
// Returns a SequenceInfo with currentIndex pointing to the given file.
// If no sequence is detected (single file, no numeric component), returns
// a SequenceInfo with an empty frames vector.
SequenceInfo DetectSequence(const std::wstring& filePath);
