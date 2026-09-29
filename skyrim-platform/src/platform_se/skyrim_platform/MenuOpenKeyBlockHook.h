#pragma once

// THORNSWOOD PATCH (Thornswood #1016). Puts MenuOpenKeyBlocks in front of the
// game's MenuOpenHandler. See MenuOpenKeyBlocks.h in platform_lib.
namespace MenuOpenKeyBlockHook {
void Install();
}
