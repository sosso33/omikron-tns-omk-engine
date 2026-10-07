// SPDX-License-Identifier: GPL-3.0-or-later
// The Android activity: SDL's own, loading libSDL2.so and libmain.so (the
// game, `android_main.cpp`). `todo/quest-port.md` §5 step 4.
package org.omk.play;

import org.libsdl.app.SDLActivity;

public class OMKActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
