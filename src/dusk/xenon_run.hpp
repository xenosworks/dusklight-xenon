#pragma once

// The Xbox 360 test run (vulkan4xenon's milestone 9): it logs each stage as it starts, the
// frame rate every 10 seconds and the game's heaps at their peaks, and ends a minute into the
// title scene.
namespace dusk::xenon_run {

// After each frame; at the end it clears dusk::IsRunning, so the game shuts down as on a quit.
void after_frame();

}  // namespace dusk::xenon_run
