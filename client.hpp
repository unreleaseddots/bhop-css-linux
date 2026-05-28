#pragma once
#include <cstdint>

// Offsets confirmados via live memory scan — CS:S Linux x64
// buildid: 17399420
namespace Offsets
{
    constexpr uintptr_t local_player = 0xfa6518;  // dwLocalPlayer
    constexpr uintptr_t m_fFlags     = 0x470;     // relativo ao objeto CBasePlayer
    constexpr uintptr_t force_jump   = 0x1017578; // dwForceJump
}

namespace Module
{
    constexpr const char* client = "client.so";
}
