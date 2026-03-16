// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for sequence detection and navigation.
// Build & run: bazelisk test //:sequence_test

#include "sequence.h"

#include <cassert>
#include <cstdio>
#include <filesystem>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                                                                                                     \
    static void test_##name();                                                                                         \
    struct Register_##name                                                                                             \
    {                                                                                                                  \
        Register_##name() { test_##name(); }                                                                           \
    } reg_##name;                                                                                                      \
    static void test_##name()

#define EXPECT(expr)                                                                                                   \
    do                                                                                                                 \
    {                                                                                                                  \
        tests_run++;                                                                                                   \
        if (expr)                                                                                                      \
        {                                                                                                              \
            tests_passed++;                                                                                            \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr);                                         \
        }                                                                                                              \
    } while (0)

namespace fs = std::filesystem;

// Helper: create a temp directory with empty files, run DetectSequence, clean up.
struct TempDir
{
    fs::path path;

    TempDir()
    {
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        path = fs::path(tmp) / L"exray_seq_test";
        fs::create_directories(path);
    }

    ~TempDir() { fs::remove_all(path); }

    void CreateFile(const wchar_t* name)
    {
        auto filePath = path / name;
        FILE* f = _wfopen(filePath.c_str(), L"w");
        if (f)
            fclose(f);
    }

    std::wstring FilePath(const wchar_t* name) { return (path / name).wstring(); }
};

// =====================================================================
// FindNearest (pure logic, no filesystem)
// =====================================================================

TEST(FindNearest_ExactMatch)
{
    SequenceInfo seq;
    seq.frames = {{1, L""}, {5, L""}, {10, L""}, {20, L""}};
    EXPECT(seq.FindNearest(5) == 1);
    EXPECT(seq.FindNearest(1) == 0);
    EXPECT(seq.FindNearest(20) == 3);
}

TEST(FindNearest_Between)
{
    SequenceInfo seq;
    seq.frames = {{1, L""}, {5, L""}, {10, L""}, {20, L""}};
    // 3 is closer to 5 (dist=2) than to 1 (dist=2) — tie goes to lower
    EXPECT(seq.FindNearest(3) == 0); // equidistant, picks lower
    EXPECT(seq.FindNearest(4) == 1); // closer to 5
    EXPECT(seq.FindNearest(7) == 1); // closer to 5
    EXPECT(seq.FindNearest(8) == 2); // closer to 10
}

TEST(FindNearest_OutOfRange)
{
    SequenceInfo seq;
    seq.frames = {{5, L""}, {10, L""}, {15, L""}};
    EXPECT(seq.FindNearest(0) == 0);  // before first
    EXPECT(seq.FindNearest(99) == 2); // after last
}

TEST(FindNearest_SingleFrame)
{
    SequenceInfo seq;
    seq.frames = {{42, L""}};
    EXPECT(seq.FindNearest(42) == 0);
    EXPECT(seq.FindNearest(1) == 0);
    EXPECT(seq.FindNearest(100) == 0);
}

// =====================================================================
// DetectSequence (filesystem-based)
// =====================================================================

TEST(DetectSequence_StandardNumbered)
{
    TempDir dir;
    dir.CreateFile(L"render.001.exr");
    dir.CreateFile(L"render.002.exr");
    dir.CreateFile(L"render.003.exr");

    auto seq = DetectSequence(dir.FilePath(L"render.002.exr"));
    EXPECT(seq.frames.size() == 3);
    EXPECT(seq.CurrentFrameNumber() == 2);
    EXPECT(seq.frames[0].frameNumber == 1);
    EXPECT(seq.frames[2].frameNumber == 3);
}

TEST(DetectSequence_WithGaps)
{
    TempDir dir;
    dir.CreateFile(L"shot.001.exr");
    dir.CreateFile(L"shot.002.exr");
    dir.CreateFile(L"shot.005.exr");
    dir.CreateFile(L"shot.010.exr");

    auto seq = DetectSequence(dir.FilePath(L"shot.005.exr"));
    EXPECT(seq.frames.size() == 4);
    EXPECT(seq.CurrentFrameNumber() == 5);
    EXPECT(seq.frames[0].frameNumber == 1);
    EXPECT(seq.frames[3].frameNumber == 10);
}

TEST(DetectSequence_SingleFile)
{
    TempDir dir;
    dir.CreateFile(L"lonely.001.exr");

    auto seq = DetectSequence(dir.FilePath(L"lonely.001.exr"));
    EXPECT(seq.frames.empty()); // single file = not a sequence
}

TEST(DetectSequence_NoDigits)
{
    TempDir dir;
    dir.CreateFile(L"photo.exr");

    auto seq = DetectSequence(dir.FilePath(L"photo.exr"));
    EXPECT(seq.frames.empty()); // no numeric component
}

TEST(DetectSequence_MultipleSequencesSameDir)
{
    TempDir dir;
    dir.CreateFile(L"beauty.001.exr");
    dir.CreateFile(L"beauty.002.exr");
    dir.CreateFile(L"depth.001.exr");
    dir.CreateFile(L"depth.002.exr");

    auto seqA = DetectSequence(dir.FilePath(L"beauty.001.exr"));
    auto seqB = DetectSequence(dir.FilePath(L"depth.001.exr"));

    EXPECT(seqA.frames.size() == 2);
    EXPECT(seqB.frames.size() == 2);
    // Each sequence only contains its own prefix
    EXPECT(seqA.frames[0].path.find(L"beauty") != std::wstring::npos);
    EXPECT(seqB.frames[0].path.find(L"depth") != std::wstring::npos);
}

TEST(DetectSequence_UnderscoreSeparator)
{
    TempDir dir;
    dir.CreateFile(L"render_001.exr");
    dir.CreateFile(L"render_002.exr");
    dir.CreateFile(L"render_003.exr");

    auto seq = DetectSequence(dir.FilePath(L"render_002.exr"));
    EXPECT(seq.frames.size() == 3);
    EXPECT(seq.CurrentFrameNumber() == 2);
}

TEST(DetectSequence_ZeroIndexed)
{
    TempDir dir;
    dir.CreateFile(L"frame.0000.exr");
    dir.CreateFile(L"frame.0001.exr");
    dir.CreateFile(L"frame.0002.exr");

    auto seq = DetectSequence(dir.FilePath(L"frame.0000.exr"));
    EXPECT(seq.frames.size() == 3);
    EXPECT(seq.CurrentFrameNumber() == 0);
    EXPECT(seq.frames[0].frameNumber == 0);
}

TEST(DetectSequence_SortedNumerically)
{
    TempDir dir;
    // Create files that would sort differently lexically vs numerically
    dir.CreateFile(L"v.1.exr");
    dir.CreateFile(L"v.2.exr");
    dir.CreateFile(L"v.10.exr");
    dir.CreateFile(L"v.20.exr");

    auto seq = DetectSequence(dir.FilePath(L"v.10.exr"));
    EXPECT(seq.frames.size() == 4);
    EXPECT(seq.frames[0].frameNumber == 1);
    EXPECT(seq.frames[1].frameNumber == 2);
    EXPECT(seq.frames[2].frameNumber == 10);
    EXPECT(seq.frames[3].frameNumber == 20);
    EXPECT(seq.currentIndex == 2); // frame 10 is at index 2
}

int main()
{
    printf("Sequence tests: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
