// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef PlutoLua_h
#define PlutoLua_h

// Central include for Pluto's C API (https://github.com/PlutoLang/Pluto).
//
// Pluto's public luaconf.h exports a set of generic ANSI colour macros
// (RED, GRN, BLU, WHT, RESET, ...) that are only used by Pluto's own
// implementation, not by its API. They clash with ordinary engine identifiers
// such as the ColorFormat::RED enum, so drop them as soon as the headers are
// in. Everything else Pluto defines is namespaced (LUA_*, LUAI_*, PLUTO_*).

#include "lua.hpp"

#undef ESC
#undef BLK
#undef RED
#undef GRN
#undef YEL
#undef BLU
#undef MAG
#undef CYN
#undef WHT
#undef BBLK
#undef BRED
#undef BGRN
#undef BYEL
#undef BBLU
#undef BMAG
#undef BCYN
#undef BWHT
#undef UBLK
#undef URED
#undef UGRN
#undef UYEL
#undef UBLU
#undef UMAG
#undef UCYN
#undef UWHT
#undef BLKB
#undef REDB
#undef GRNB
#undef YELB
#undef BLUB
#undef MAGB
#undef CYNB
#undef WHTB
#undef BLKHB
#undef REDHB
#undef GRNHB
#undef YELHB
#undef BLUHB
#undef MAGHB
#undef CYNHB
#undef WHTHB
#undef HBLK
#undef HRED
#undef HGRN
#undef HYEL
#undef HBLU
#undef HMAG
#undef HCYN
#undef HWHT
#undef BHBLK
#undef BHRED
#undef BHGRN
#undef BHYEL
#undef BHBLU
#undef BHMAG
#undef BHCYN
#undef BHWHT
#undef RESET
#undef CRESET
#undef COLOR_RESET

#endif // PlutoLua_h
