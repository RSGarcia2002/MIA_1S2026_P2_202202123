#pragma once
#include <string>
#include <vector>

// Operaciones sobre archivos y carpetas del sistema EXT2 (Sprint 4)
namespace FileOperations
{
    std::string Mkfile(const std::string &path, bool recursive, int size, const std::string &cont);
    std::string Edit(const std::string &path, const std::string &cont);
    std::string Mkdir(const std::string &path, bool parents);
    std::string Cat(const std::vector<std::string> &files);
    std::string Remove(const std::string &path);
    std::string Rename(const std::string &path, const std::string &name);
    std::string Copy(const std::string &path, const std::string &destino);
    std::string Move(const std::string &path, const std::string &destino);
    std::string Find(const std::string &path, const std::string &name);
    std::string Chown(const std::string &path, const std::string &user, bool recursive);
    std::string Chmod(const std::string &path, const std::string &ugo, bool recursive);
    std::string BrowseJson(const std::string &id, const std::string &path);
    std::string ReadFileJson(const std::string &id, const std::string &path);
}
