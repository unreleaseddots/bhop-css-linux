bhop-css-linux

⚠️ Educational purposes only. May be outdated or broken on newer builds. This project was built to study external process memory manipulation on Linux.

External bunny hop script for Counter-Strike: Source on Linux, written in C++.

How it works
This is a fully external cheat — it never injects into the game process. Instead it:

Reads the game's memory via process_vm_readv to get the local player pointer and jump flags
Writes to the force_jump offset via process_vm_writev to simulate spacebar inputs at the right timing
Reads keyboard input directly from /dev/input/eventX without relying on X11 or any window focus

The bhop pulse timing is 28ms on / 2ms off, with a value transition of 5 → 4 required per jump cycle.

Offsets
Tested on CS:S buildid 17399420 (client.so):
SymbolOffsetdwLocalPlayer0xFA6518force_jump0x1017578

These offsets will break on different builds. If the chop isn't working, the offsets are probably outdated.


Stack

C++ (external, no injection)
process_vm_readv / process_vm_writev — memory R/W
/dev/input/eventX — raw keyboard input
Linux only


Building
bashmake
Run as root (required for /proc memory access and /dev/input):
bashsudo ./bhop

Disclaimer
This project was built for educational purposes to study Linux process memory and low-level input handling. Using cheats in online games violates terms of service. Don't be an idiot.
