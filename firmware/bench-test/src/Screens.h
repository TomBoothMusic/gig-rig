// The footswitch screens on the controller (Teensy 4.1). Guide: docs/switch-screens.md.
#pragma once

namespace screens
{

/** Call once in setup(): wakes every screen and shows the first sample page. */
void begin();

/** Call on every pass of loop(). Does one small piece of work (drawing one tile, or sending a
    slice of it to its screen) and returns within about 2 ms, so the kick pad is never kept waiting. */
void service();

/** A read-out across the bottom of screen 1 for a few seconds (the kick pad's "hit" numbers). */
void note (const char* big, const char* small);

}
