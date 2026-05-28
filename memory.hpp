#pragma once

#include <string>
#include <cstdint>
#include <sys/types.h>

namespace Memory
{
    /**
     * Lê `size` bytes do processo `pid` a partir de `addr`,
     * armazenando o resultado em `buffer`.
     * Retorna true em caso de sucesso.
     */
    bool read(pid_t pid, uintptr_t addr, void* buffer, size_t size);

    /**
     * Escreve `size` bytes de `buffer` no processo `pid`
     * a partir de `addr`.
     * Retorna true em caso de sucesso.
     */
    bool write(pid_t pid, uintptr_t addr, void* buffer, size_t size);

    /**
     * Resolve o endereço base de um módulo (shared library / executável)
     * lendo `/proc/<pid>/maps`.
     * Retorna 0 se o módulo não for encontrado.
     */
    uintptr_t module_base_address(pid_t pid, const std::string& module);

} // namespace Memory
