// SPDX-License-Identifier: GPL-3.0-or-later
// The Vita's on-screen keyboard - see `ime.h`.
#include "ime.h"

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>
#include <vitaGL.h>

#include <cstring>
#include <vector>

namespace omk::vita {

bool imeEdit(const char* title, const std::string& initial, int maxLen, std::string& out) {
    if (maxLen < 1) maxLen = 1;
    if (maxLen > SCE_IME_DIALOG_MAX_TEXT_LENGTH) maxLen = SCE_IME_DIALOG_MAX_TEXT_LENGTH;

    // Latin-1 <-> UTF-16 is a widening and a narrowing: the code points are
    // the same below 256.
    SceWChar16 title16[SCE_IME_DIALOG_MAX_TITLE_LENGTH] = {};
    for (std::size_t i = 0; title[i] && i + 1 < SCE_IME_DIALOG_MAX_TITLE_LENGTH; ++i)
        title16[i] = static_cast<unsigned char>(title[i]);
    std::vector<SceWChar16> init16(static_cast<std::size_t>(maxLen) + 1, 0);
    for (std::size_t i = 0; i < initial.size() && i < static_cast<std::size_t>(maxLen); ++i)
        init16[i] = static_cast<unsigned char>(initial[i]);
    std::vector<SceWChar16> buf16(static_cast<std::size_t>(maxLen) + 1, 0);

    SceImeDialogParam p;
    sceImeDialogParamInit(&p);
    p.supportedLanguages = 0;          // the system's own set
    p.languagesForced = SCE_FALSE;
    p.type = SCE_IME_TYPE_BASIC_LATIN;
    p.title = title16;
    p.maxTextLength = static_cast<SceUInt32>(maxLen);
    p.initialText = init16.data();
    p.inputTextBuffer = buf16.data();
    if (sceImeDialogInit(&p) < 0) return false;

    // Modal: keep the display alive until the dialog finishes. vitaGL draws
    // the common dialog on its swap when told there is one (GL_TRUE).
    bool ok = false;
    for (;;) {
        const SceCommonDialogStatus st = sceImeDialogGetStatus();
        if (st == SCE_COMMON_DIALOG_STATUS_FINISHED) {
            SceImeDialogResult r;
            std::memset(&r, 0, sizeof r);
            sceImeDialogGetResult(&r);
            ok = r.button == SCE_IME_DIALOG_BUTTON_ENTER;
            break;
        }
        if (st != SCE_COMMON_DIALOG_STATUS_RUNNING) break;   // nothing to wait for
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        vglSwapBuffers(GL_TRUE);
    }
    sceImeDialogTerm();
    if (!ok) return false;
    out.clear();
    for (std::size_t i = 0; i < buf16.size() && buf16[i]; ++i)
        if (buf16[i] < 256) out.push_back(static_cast<char>(buf16[i]));
    return true;
}

}  // namespace omk::vita
