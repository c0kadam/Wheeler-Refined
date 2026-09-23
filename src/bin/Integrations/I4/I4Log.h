#pragma once

// I4 integration internal logging is intentionally disabled.
// Keep these no-op macros to avoid runtime formatting overhead while retaining call sites.
#define I4_LOG_INFO(...) ((void)0)
#define I4_LOG_WARN(...) ((void)0)
