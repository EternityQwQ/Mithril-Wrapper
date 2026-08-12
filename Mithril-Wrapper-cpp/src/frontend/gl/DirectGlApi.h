#pragma once

namespace mithril::frontend::gl {

using GlProc = void (*)(void);
[[nodiscard]] GlProc lookupDirectGlProc(const char* name) noexcept;

} // namespace mithril::frontend::gl
