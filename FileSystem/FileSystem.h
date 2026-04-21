#pragma once
#include <string>

// Formateo de particiones EXT2 (Sprint 2)
namespace FileSystem
{
    // Formatea la particion indicada por el ID.
    // type: full|fast, fs: 2fs|3fs
    std::string Mkfs(const std::string &id, const std::string &type, const std::string &fs);

    // Simula perdida del sistema de archivos (EXT3).
    std::string Loss(const std::string &id);

    // Muestra el contenido del journal (EXT3).
    std::string Journaling(const std::string &id);

    // Recupera la estructura desde journal (EXT3).
    std::string Recovery(const std::string &id);

    // Registro de operaciones para EXT3.
    void AutoJournalFromCommand(const std::string &line, const std::string &result);
}
