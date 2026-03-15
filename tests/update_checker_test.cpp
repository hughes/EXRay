// SPDX-License-Identifier: GPL-3.0-or-later

// Unit tests for UpdateChecker semver parsing (no network calls).
// Build & run: bazelisk test //:update_checker_test

#include "update_checker.h"

#include <cassert>
#include <cstdio>
#include <cstring>

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

// --- PackVersion ---

TEST(PackVersion_basic)
{
    EXPECT(UpdateChecker::PackVersion(0, 1, 0) > 0);
    EXPECT(UpdateChecker::PackVersion(1, 0, 0) > UpdateChecker::PackVersion(0, 99, 99));
}

TEST(PackVersion_ordering)
{
    EXPECT(UpdateChecker::PackVersion(0, 2, 0) > UpdateChecker::PackVersion(0, 1, 0));
    EXPECT(UpdateChecker::PackVersion(0, 1, 1) > UpdateChecker::PackVersion(0, 1, 0));
    EXPECT(UpdateChecker::PackVersion(1, 0, 0) > UpdateChecker::PackVersion(0, 9, 9));
    EXPECT(UpdateChecker::PackVersion(0, 1, 0) == UpdateChecker::PackVersion(0, 1, 0));
}

// --- ParseTagVersion ---

TEST(ParseTagVersion_standard)
{
    const char* json = R"({"tag_name": "v1.2.3", "name": "Release 1.2.3"})";
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(packed == UpdateChecker::PackVersion(1, 2, 3));
    EXPECT(strcmp(ver, "1.2.3") == 0);
}

TEST(ParseTagVersion_no_v_prefix)
{
    const char* json = R"({"tag_name": "2.0.1"})";
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(packed == UpdateChecker::PackVersion(2, 0, 1));
    EXPECT(strcmp(ver, "2.0.1") == 0);
}

TEST(ParseTagVersion_major_minor_only)
{
    // sscanf with %d.%d.%d should parse two integers, patch defaults to 0
    const char* json = R"({"tag_name": "v3.5"})";
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(packed == UpdateChecker::PackVersion(3, 5, 0));
}

TEST(ParseTagVersion_zero_version)
{
    const char* json = R"({"tag_name": "v0.0.0"})";
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(packed == 0);
}

TEST(ParseTagVersion_realistic_github_response)
{
    // Subset of a real GitHub API response
    const char* json = R"({"url":"https://api.github.com/repos/hughes/EXRay/releases/1234",)"
                       R"("tag_name":"v0.2.0","target_commitish":"main","name":"v0.2.0",)"
                       R"("draft":false,"prerelease":false,"body":"Release notes here"})";
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(packed == UpdateChecker::PackVersion(0, 2, 0));
    EXPECT(strcmp(ver, "0.2.0") == 0);
}

TEST(ParseTagVersion_missing_tag_name)
{
    const char* json = R"({"name": "v1.0.0"})";
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(packed == 0);
    EXPECT(ver[0] == '\0');
}

TEST(ParseTagVersion_empty_string)
{
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion("", ver, sizeof(ver));
    EXPECT(packed == 0);
}

TEST(ParseTagVersion_garbage)
{
    const char* json = R"({"tag_name": "not-a-version"})";
    char ver[32] = {};
    int packed = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(packed == 0);
}

TEST(ParseTagVersion_null_version_out)
{
    const char* json = R"({"tag_name": "v1.0.0"})";
    int packed = UpdateChecker::ParseTagVersion(json, nullptr, 0);
    EXPECT(packed == UpdateChecker::PackVersion(1, 0, 0));
}

// --- Simulated update check logic (no network) ---

TEST(UpdateLogic_newer_version_detected)
{
    int current = UpdateChecker::PackVersion(0, 1, 0);
    const char* json = R"({"tag_name": "v0.2.0"})";
    char ver[32] = {};
    int remote = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(remote > current);
}

TEST(UpdateLogic_same_version)
{
    int current = UpdateChecker::PackVersion(0, 1, 0);
    const char* json = R"({"tag_name": "v0.1.0"})";
    char ver[32] = {};
    int remote = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(!(remote > current));
}

TEST(UpdateLogic_older_version)
{
    int current = UpdateChecker::PackVersion(1, 0, 0);
    const char* json = R"({"tag_name": "v0.9.9"})";
    char ver[32] = {};
    int remote = UpdateChecker::ParseTagVersion(json, ver, sizeof(ver));
    EXPECT(!(remote > current));
}

int main()
{
    // Tests already ran via static initialization
    printf("update_checker_test: %d/%d passed\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
