/*
 * SimDistanceUnlock - lets the simulation distance (演算距離) be set freely.
 *
 * Minecraft Bedrock 1.26.60.4 (arm64) limits simulation distance in two
 * places; this mod rewrites a few constants in both at load time:
 *
 *  1. ui_list     The settings screen builds its option list as
 *                 4, 6, 8, ... up to a maximum the game reports.
 *                 -> start value, step and maximum are replaced.
 *  2. level_clamp When a world's level.dat is read, "serverChunkTickRange"
 *                 is clamped to [4, 12].
 *                 -> the clamp bounds are replaced.
 *
 * Every site is located by byte signature (the patched instructions are
 * wildcarded, so the signature matches both before and after patching) and
 * its original instruction is verified before writing, so a game build that differs simply leaves the
 * game untouched (load() returns false) - it can never write blindly.
 *
 * Edit the three constants below to change the range.
 */
#include <android/log.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

#include "pl/Mod.hpp"
#include "pl/memory/Patch.hpp"
#include "pl/memory/Signature.hpp"

/* ------------------------------ settings -------------------------------- */
constexpr uint32_t kMinDistance = 1;  /* smallest selectable value            */
constexpr uint32_t kMaxDistance = 32; /* largest selectable value (>= 4)      */
constexpr uint32_t kStep = 1;         /* spacing between options in the list  */
/* ------------------------------------------------------------------------ */

namespace {

constexpr const char *kTag = "SimDistanceUnlock";

/* AArch64 encoders (32-bit registers) */
constexpr uint32_t movzW(uint32_t rd, uint32_t imm) {
    return 0x52800000u | (imm << 5) | rd;
}
constexpr uint32_t addImmW(uint32_t rd, uint32_t rn, uint32_t imm) {
    return 0x11000000u | (imm << 10) | (rn << 5) | rd;
}
constexpr uint32_t cmpImmW(uint32_t rn, uint32_t imm) {
    return 0x7100001Fu | (imm << 10) | (rn << 5);
}

/* The encoders must reproduce the game's original instructions exactly. */
static_assert(movzW(26, 4) == 0x5280009Au);
static_assert(addImmW(26, 26, 2) == 0x11000B5Au);
static_assert(cmpImmW(0, 4) == 0x7100101Fu);
static_assert(movzW(8, 4) == 0x52800088u);
static_assert(movzW(9, 12) == 0x52800189u);
static_assert(cmpImmW(8, 12) == 0x7100311Fu);

static_assert(kMinDistance >= 1, "kMinDistance must be >= 1");
static_assert(kMaxDistance >= 4 && kMaxDistance <= 255,
              "kMaxDistance must be in 4..255 (the game needs >= 4 to build the list)");
static_assert(kMaxDistance >= kMinDistance, "kMaxDistance < kMinDistance");
static_assert(kStep >= 1 && kStep <= 16, "kStep must be 1..16");

constexpr std::array<uint8_t, 4> le(uint32_t w) {
    return {static_cast<uint8_t>(w), static_cast<uint8_t>(w >> 8),
            static_cast<uint8_t>(w >> 16), static_cast<uint8_t>(w >> 24)};
}

struct PatchSite {
    const char *name;
    const char *signature;
    size_t patchOffset; /* byte offset of the 4-byte instruction in the match */
    std::array<uint8_t, 4> original;
    std::array<uint8_t, 4> patch;
};

/* Group 1: settings-screen option list builder (1.26.60.4: 0xA1B3420) */
const char *const kSigUiList =
    "00 01 3F D6 ?? ?? ?? ?? FF 7F 02 A9 ?? ?? ?? ?? "
    "C1 22 00 91 00 00 80 92 ?? ?? ?? ?? ?? ?? ?? ?? "
    "C8 02 40 F9 E0 03 16 AA 08 09 40 F9 00 01 3F D6 "
    "E0 03 16 AA ?? ?? ?? ?? F6 17 40 F9 FF 1B 00 F9 "
    "?? ?? ?? ?? C1 22 00 91 00 00 80 92 ?? ?? ?? ?? "
    "?? ?? ?? ?? C8 02 40 F9 E0 03 16 AA 08 09 40 F9 "
    "00 01 3F D6 E0 03 16 AA ?? ?? ?? ?? E8 63 40 39 "
    "?? ?? ?? ?? E0 0B 40 F9 ?? ?? ?? ?? BF 12 00 71 "
    "?? ?? ?? ?? E8 03 40 F9 08 15 40 F9 A9 83 5F F8 "
    "1F 01 09 EB ?? ?? ?? ?? F4 4F 49 A9 F6 57 48 A9 "
    "F8 5F 47 A9 FA 67 46 A9 FC 6F 45 A9 FD 7B 44 A9 "
    "FF 83 02 91 C0 03 5F D6 79 12 40 F9 ?? ?? ?? ?? "
    "FB F3 7E B2 1C 00 F8 92 ?? ?? ?? ?? 3A 47 00 B8 "
    "?? ?? ?? ?? 79 12 00 F9 5F 03 15 6B ?? ?? ?? ?? "
    "68 16 40 F9 3F 03 08 EB";

/* Group 2: level.dat "serverChunkTickRange" clamp [4,12] (0x11132280) */
const char *const kSigLevelClamp =
    "E0 03 14 AA 82 02 80 52 ?? ?? ?? ?? ?? ?? ?? ?? "
    "?? ?? ?? ?? 21 34 0F 91 E0 03 14 AA 82 02 80 52 "
    "?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? ?? "
    "08 C0 88 1A ?? ?? ?? ?? 08 31 89 1A ?? ?? ?? ?? "
    "88 00 80 52 68 32 03 B9";

const PatchSite kSites[] = {
    /* mov w21, w0  ->  mov w21, #kMaxDistance   (upper bound of the list) */
    {"simdist.ui_max", kSigUiList, 0x04, le(0x2A0003F5u),
     le(movzW(21, kMaxDistance))},
    /* mov w26, #4  ->  mov w26, #kMinDistance   (first option) */
    {"simdist.ui_start", kSigUiList, 0xBC, le(movzW(26, 4)),
     le(movzW(26, kMinDistance))},
    /* add w26, w26, #2  ->  add w26, w26, #kStep */
    {"simdist.ui_step", kSigUiList, 0xD0, le(addImmW(26, 26, 2)),
     le(addImmW(26, 26, kStep))},

    /* cmp w0, #4 ; mov w8, #4  : lower bound of the level.dat clamp */
    {"simdist.lv_cmp_min", kSigLevelClamp, 0x24, le(cmpImmW(0, 4)),
     le(cmpImmW(0, kMinDistance))},
    {"simdist.lv_min", kSigLevelClamp, 0x28, le(movzW(8, 4)),
     le(movzW(8, kMinDistance))},
    /* mov w9, #12 ; cmp w8, #12 : upper bound of the clamp */
    {"simdist.lv_max", kSigLevelClamp, 0x2C, le(movzW(9, 12)),
     le(movzW(9, kMaxDistance))},
    {"simdist.lv_cmp_max", kSigLevelClamp, 0x34, le(cmpImmW(8, 12)),
     le(cmpImmW(8, kMaxDistance))},
};

void logi(const char *fmt, const char *a) {
    __android_log_print(ANDROID_LOG_INFO, kTag, fmt, a);
}

enum class State { Original, Patched, Bad };

struct Resolved {
    uintptr_t addr = 0;
    State state = State::Bad;
};

/* Read-only check: locate the site and classify the 4 bytes found there. */
Resolved inspect(const PatchSite &s) {
    Resolved r;
    const uintptr_t base =
        pl::memory::resolveSignature(s.signature, "libminecraftpe.so");
    if (base == 0) return r; /* signature not found: different game build */
    r.addr = base + s.patchOffset;
    const auto cur = pl::memory::readBytes(r.addr, 4);
    if (cur.size() != 4) return r;
    if (std::memcmp(cur.data(), s.patch.data(), 4) == 0)
        r.state = State::Patched;
    else if (std::memcmp(cur.data(), s.original.data(), 4) == 0)
        r.state = State::Original;
    return r;
}

} // namespace

class SimDistanceUnlockMod {
public:
    /* All-or-nothing: every site is inspected first; if any one does not
     * match this game build, nothing at all is written. */
    bool load() {
        constexpr size_t kCount = sizeof(kSites) / sizeof(kSites[0]);
        Resolved found[kCount];
        bool ok = true;
        for (size_t i = 0; i < kCount; ++i) {
            found[i] = inspect(kSites[i]);
            if (found[i].state == State::Bad) {
                ok = false;
                __android_log_print(ANDROID_LOG_WARN, kTag,
                                    "site %s does not match this build",
                                    kSites[i].name);
            }
        }
        if (!ok) {
            logi("%s", "game build differs - nothing was changed");
            return false;
        }

        for (size_t i = 0; i < kCount; ++i) {
            if (found[i].state == State::Patched) continue;
            if (!pl::memory::writeBytes(found[i].addr, kSites[i].patch,
                                        kSites[i].name)) {
                __android_log_print(ANDROID_LOG_ERROR, kTag,
                                    "write failed at %s", kSites[i].name);
                ok = false;
            }
        }
        logi("%s", ok ? "all sites patched" : "write error (see log)");
        return ok;
    }
};

static SimDistanceUnlockMod mod;
PL_REGISTER_MOD(SimDistanceUnlockMod, mod)
