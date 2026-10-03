#pragma once

namespace FastThreadSnapshot
{
    void Install();   // before MinHook's first use: its thread freeze then lists only our threads, not the whole PC's
}
