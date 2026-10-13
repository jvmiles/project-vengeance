// SPDX-License-Identifier: GPL-2.0-only
// Xara LX port diagnostic; distributed as part of this Xara LX derivative.
// ABI declarations mirror GDraw/gdraw.h without requiring wxWidgets headers.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "GVersion.h"
struct GCONTEXT;
extern "C" {
std::uint32_t GDraw_GetVersion();
std::int32_t GDraw_ContextLength();
std::int32_t GDraw_Initialise(GCONTEXT*, const GCONTEXT*);
std::int32_t GDraw_Terminate(GCONTEXT*);
}
int main() {
    static_assert(sizeof(void*) == 8, "This probe tests the x86_64 archive");
    const auto version = GDraw_GetVersion();
    const auto length = GDraw_ContextLength();
    std::printf("CDraw version %u.%u; context %d bytes\n",
                version >> 16, version & 0xffff, length);
    if ((version >> 16) < GDRAW_VERSION_MAJOR || length <= 0) return 1;
    auto* context = static_cast<GCONTEXT*>(std::calloc(1, length));
    if (!context) return 1;
    const auto init = GDraw_Initialise(context, nullptr);
    std::printf("Initialise returned %d\n", init);
    const auto term = GDraw_Terminate(context);
    std::printf("Terminate returned %d\n", term);
    std::free(context);
    return (init != 0 || term != 0) ? 1 : 0;
}
