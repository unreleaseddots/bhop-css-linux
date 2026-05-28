#include "memory.hpp"

#include <sys/uio.h>    // process_vm_readv / process_vm_writev
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>

namespace Memory
{

// ─────────────────────────────────────────────
//  read
// ─────────────────────────────────────────────
bool read(pid_t pid, uintptr_t addr, void* buffer, size_t size)
{
    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base  = buffer;
    local[0].iov_len   = size;
    remote[0].iov_base = reinterpret_cast<void*>(addr);
    remote[0].iov_len  = size;

    ssize_t n = process_vm_readv(pid, local, 1, remote, 1, 0);
    return n == static_cast<ssize_t>(size);
}

// ─────────────────────────────────────────────
//  write
// ─────────────────────────────────────────────
bool write(pid_t pid, uintptr_t addr, void* buffer, size_t size)
{
    struct iovec local[1];
    struct iovec remote[1];

    local[0].iov_base  = buffer;
    local[0].iov_len   = size;
    remote[0].iov_base = reinterpret_cast<void*>(addr);
    remote[0].iov_len  = size;

    ssize_t n = process_vm_writev(pid, local, 1, remote, 1, 0);
    return n == static_cast<ssize_t>(size);
}

// ─────────────────────────────────────────────
//  module_base_address
//  Lê /proc/<pid>/maps procurando pela primeira
//  entrada que contenha o nome do módulo.
// ─────────────────────────────────────────────
uintptr_t module_base_address(pid_t pid, const std::string& module)
{
    std::string maps_path = "/proc/" + std::to_string(pid) + "/maps";
    std::ifstream maps(maps_path);

    if (!maps.is_open())
        return 0;

    std::string line;
    while (std::getline(maps, line))
    {
        // Cada linha tem o formato:
        // address           perms offset  dev   inode  pathname
        // 7f1234560000-...  r-xp  00000000 ...        /path/to/lib.so
        if (line.find(module) == std::string::npos)
            continue;

        // Extrai o endereço base (antes do '-')
        std::istringstream ss(line);
        std::string range;
        ss >> range;

        auto dash = range.find('-');
        if (dash == std::string::npos)
            continue;

        uintptr_t base = std::stoull(range.substr(0, dash), nullptr, 16);
        return base;
    }

    return 0;
}

} // namespace Memory
