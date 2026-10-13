// Theme.h - palet warna terminal Ubuntu (truecolor) yang dipakai bersama
// oleh nxfire, nxfire-client, dan alat lain.
//
// Warna hanya aktif bila stdout adalah terminal (TTY), variabel NO_COLOR
// tidak diisi, dan tidak dimatikan lewat --no-color.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
// winsock2.h sebelum windows.h untuk menghindari bentrok definisi.
#include <winsock2.h>
#include <windows.h>
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#include <io.h>
#else
#include <unistd.h>
#endif

namespace nxfire {
namespace theme {

// --- Palet terminal Ubuntu ------------------------------------------------
inline constexpr const char* RESET   = "\x1b[0m";
inline constexpr const char* ORANGE  = "\x1b[1;38;2;233;84;32m";   // #E95420
inline constexpr const char* GREEN   = "\x1b[1;38;2;138;226;52m";  // #8AE234
inline constexpr const char* RED     = "\x1b[1;38;2;239;41;41m";   // #EF2929
inline constexpr const char* BLUE    = "\x1b[1;38;2;114;159;207m"; // #729FCF
inline constexpr const char* CYAN    = "\x1b[1;38;2;52;226;226m";  // #34E2E2
inline constexpr const char* YELLOW  = "\x1b[38;2;252;233;79m";    // #FCE94F
inline constexpr const char* MAGENTA = "\x1b[38;2;173;127;168m";   // #AD7FA8
inline constexpr const char* GRAY    = "\x1b[38;2;136;138;133m";   // #888A85
inline constexpr const char* PURPLE  = "\x1b[1;38;2;173;127;168m"; // #AD7FA8 (aubergine terang)
inline constexpr const char* WHITE   = "\x1b[1;38;2;238;238;236m"; // #EEEEEC

inline bool& state() { static bool on = false; return on; }

inline bool stdoutIsTty() {
#ifdef _WIN32
    if (!_isatty(_fileno(stdout))) return false;
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    return true;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

inline bool envNoColor() {
    const char* nc = std::getenv("NO_COLOR");
    return nc && *nc;
}

// on=true paksa warna, on=false paksa mati, default: auto (TTY & !NO_COLOR).
inline void autoDetect() { state() = stdoutIsTty() && !envNoColor(); }
inline void set(bool on) { state() = on; }
inline bool on() { return state(); }

// Bungkus teks dengan warna (kosong bila warna nonaktif).
inline std::string c(const char* code, const std::string& s) {
    return state() ? std::string(code) + s + RESET : s;
}
inline std::string c(const char* code) { return state() ? std::string(code) : std::string(); }

} // namespace theme
} // namespace nxfire
