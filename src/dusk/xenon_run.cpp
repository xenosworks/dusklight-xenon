#include "dusk/xenon_run.hpp"

#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JKernel/JKRSolidHeap.h"
#include "d/d_com_inf_game.h"
#include "dusk/logging.h"
#include "dusk/main.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_ext.h"

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <malloc.h>
#include <xsdk/kernel.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace dusk::xenon_run {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* TitleStage = "F_SP102";
constexpr Clock::duration TitleTime = std::chrono::seconds(60);
constexpr Clock::duration FrameRatePeriod = std::chrono::seconds(10);
// MmQueryStatistics counts 4 KB pages (xenia:src/xenia/kernel/xboxkrnl/xboxkrnl_memory.cc:496).
constexpr u32 PagesPerMB = 1024 * 1024 / 4096;

// The heaps the game makes at start, each with its peak (m_Do_machine.cpp, mDoMch_Create;
// m_Do_main.cpp, main01).
struct HeapPeak {
    const char* name;
    JKRHeap* (*get)();
    u32 size;
    u32 peak;
};

HeapPeak heaps[] = {
    {"root", [] { return JKRGetRootHeap(); }},
    {"system", [] { return JKRGetSystemHeap(); }},
    {"zelda", [] { return static_cast<JKRHeap*>(mDoExt_getZeldaHeap()); }},
    {"game", [] { return static_cast<JKRHeap*>(mDoExt_getGameHeap()); }},
    {"archive", [] { return static_cast<JKRHeap*>(mDoExt_getArchiveHeap()); }},
    {"j2d", [] { return static_cast<JKRHeap*>(mDoExt_getJ2dHeap()); }},
    {"command", [] { return static_cast<JKRHeap*>(mDoExt_getCommandHeap()); }},
    {"dbPrint", [] { return static_cast<JKRHeap*>(mDoExt_getDbPrintHeap()); }},
    {"hostIO", [] { return static_cast<JKRHeap*>(mDoExt_getHostIOHeap()); }},
    {"audio", [] { return static_cast<JKRHeap*>(g_mDoAud_audioHeap); }},
};

// newlib's heap, which holds the game's memory, Aurora's and everything else's.
u32 cHeapPeak = 0;

// Aurora's frame pools, each with the most a frame recorded into it (AURORA_*_POOL_SIZE).
struct PoolPeak {
    const char* name;
    uint32_t AuroraStats::* size;
    uint32_t peak;
};

PoolPeak pools[] = {
    {"vertex", &AuroraStats::lastVertSize},
    {"uniform", &AuroraStats::lastUniformSize},
    {"index", &AuroraStats::lastIndexSize},
    {"storage", &AuroraStats::lastStorageSize},
    {"texture upload", &AuroraStats::lastTextureUploadSize},
};

std::string stage;
bool started = false;
Clock::time_point periodStart;
u32 periodFrames = 0;
bool inTitle = false;
Clock::time_point titleStart;

// The title scene's frame this many in, beside the log (aurora_capture_frame): Xenia shows none of vulkan4xenon's
// frames, as it has no display controller (xsdk's CLAUDE.md), so a capture is how a run there shows one. Counted in
// frames, not time, so two runs capture the same frame.
constexpr u32 TitleCaptureFrame = 600;
constexpr const char* TitleCapturePath = "game:\\dusklight_title.ppm";
u32 titleFrames = 0;
bool titleCaptured = false;
bool titleLogged = false;

void sample_heaps() {
    for (HeapPeak& heap : heaps) {
        JKRHeap* const jkrHeap = heap.get();
        if (jkrHeap == nullptr) {
            continue;
        }
        heap.size = jkrHeap->getHeapSize();
        heap.peak = std::max(heap.peak, heap.size - static_cast<u32>(jkrHeap->getTotalFreeSize()));
    }
    cHeapPeak = std::max(cHeapPeak, static_cast<u32>(mallinfo().uordblks));
    const AuroraStats* const stats = aurora_get_stats();
    for (PoolPeak& pool : pools) {
        pool.peak = std::max(pool.peak, stats->*pool.size);
    }
}

// The title's memory as the kernel counts it, exactly on the console though not in Xenia (xsdk's
// CLAUDE.md), and newlib's heap. GPU memory is physical pages; newlib's heap grows through sbrk.
void log_memory() {
    MM_QUERY_STATISTICS_RESULT statistics{};
    statistics.size = sizeof(statistics);
    if (const NTSTATUS status = MmQueryStatistics(&statistics); !NT_SUCCESS(status)) {
        DuskLog.error("MmQueryStatistics failed: {:#010x}", static_cast<u32>(status));
        return;
    }
    const MM_QUERY_STATISTICS_SECTION& title = statistics.title;
    const struct mallinfo heap = mallinfo();
    DuskLog.info(
        "Memory: {} MB free; the title's pages: {} MB physical, {} MB virtual, {} MB heap, {} MB "
        "stack; newlib's heap {} MB, {} MB of it in use",
        title.available_pages / PagesPerMB, title.physical_pages / PagesPerMB,
        title.virtual_pages / PagesPerMB, title.heap_pages / PagesPerMB,
        title.stack_pages / PagesPerMB, static_cast<u32>(heap.arena) >> 20,
        static_cast<u32>(heap.uordblks) >> 20);
}

}  // namespace

void after_frame() {
    const Clock::time_point now = Clock::now();
    if (!started) {
        started = true;
        periodStart = now;
    }
    sample_heaps();

    const char* const current = dComIfGp_getStartStageName();
    if (stage != current) {
        stage = current;
        DuskLog.info("Stage: {}", stage.empty() ? "none" : stage);
        log_memory();
        if (!inTitle && stage == TitleStage) {
            inTitle = true;
            titleStart = now;
        }
    }

    if (inTitle && !titleCaptured && ++titleFrames >= TitleCaptureFrame) {
        titleCaptured = true;
        aurora_capture_frame(TitleCapturePath);
    }

    periodFrames++;
    if (now - periodStart >= FrameRatePeriod) {
        const double seconds = std::chrono::duration<double>(now - periodStart).count();
        DuskLog.info(
            "{} frames in {:.1f} s, {:.1f} fps", periodFrames, seconds, periodFrames / seconds);
        log_memory();
        periodStart = now;
        periodFrames = 0;
    }

    // The game plays on: whoever runs it ends it.
    if (inTitle && !titleLogged && now - titleStart >= TitleTime) {
        titleLogged = true;
        DuskLog.info("A minute into the title scene. The heaps at their peaks:");
        for (const HeapPeak& heap : heaps) {
            DuskLog.info("  {}: {} KB of {} KB", heap.name, heap.peak / 1024, heap.size / 1024);
        }
        DuskLog.info("  newlib's: {} KB", cHeapPeak / 1024);
        DuskLog.info("Aurora's frame pools at their peaks, a frame's:");
        for (const PoolPeak& pool : pools) {
            DuskLog.info("  {}: {} KB", pool.name, (pool.peak + 1023) / 1024);
        }
    }
}

}  // namespace dusk::xenon_run
