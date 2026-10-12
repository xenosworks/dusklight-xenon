#include "dusk/xenon_run.hpp"

#include "JSystem/JKernel/JKRExpHeap.h"
#include "JSystem/JKernel/JKRHeap.h"
#include "JSystem/JKernel/JKRSolidHeap.h"
#include "d/d_com_inf_game.h"
#include "dusk/logging.h"
#include "dusk/main.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_ext.h"

#include <malloc.h>

#include <algorithm>
#include <chrono>
#include <string>

namespace dusk::xenon_run {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* TitleStage = "F_SP102";
constexpr Clock::duration TitleTime = std::chrono::seconds(60);
constexpr Clock::duration FrameRatePeriod = std::chrono::seconds(10);

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

std::string stage;
bool started = false;
Clock::time_point periodStart;
u32 periodFrames = 0;
bool inTitle = false;
Clock::time_point titleStart;

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
        if (!inTitle && stage == TitleStage) {
            inTitle = true;
            titleStart = now;
        }
    }

    periodFrames++;
    if (now - periodStart >= FrameRatePeriod) {
        const double seconds = std::chrono::duration<double>(now - periodStart).count();
        DuskLog.info(
            "{} frames in {:.1f} s, {:.1f} fps", periodFrames, seconds, periodFrames / seconds);
        periodStart = now;
        periodFrames = 0;
    }

    if (inTitle && now - titleStart >= TitleTime) {
        DuskLog.info("A minute into the title scene. The heaps at their peaks:");
        for (const HeapPeak& heap : heaps) {
            DuskLog.info("  {}: {} KB of {} KB", heap.name, heap.peak / 1024, heap.size / 1024);
        }
        DuskLog.info("  newlib's: {} KB", cHeapPeak / 1024);
        dusk::IsRunning = false;
    }
}

}  // namespace dusk::xenon_run
