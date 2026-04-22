// FileOperations: implementa MKDIR, MKFILE y CAT sobre el sistema EXT2 activo.
// Todas las operaciones trabajan sobre la particion de la sesion activa.
#include "FileOperations.h"
#include "../UserSession/UserSession.h"
#include "../DiskManagement/DiskManagement.h"
#include "../Utilities/Utilities.h"
#include "../Structs/Structs.h"

#include <sstream>
#include <fstream>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <queue>
#include <set>
#include <map>

namespace FileOperations
{

    // ================================================================
    //  Helpers internos — acceso al disco
    // ================================================================

    // Obtiene inicio y tamanio del area de particion EXT2 segun el MountedPartition
    static bool GetPartBounds(std::fstream &f,
                              const DiskManagement::MountedPartition &mp,
                              int &partStart, int &partSize)
    {
        if (mp.isLogical)
        {
            EBR ebr{};
            if (!Utilities::ReadObject(f, ebr, mp.ebrPos))
                return false;
            partStart = ebr.Start;
            partSize = ebr.Size;
        }
        else
        {
            MBR mbr{};
            if (!Utilities::ReadObject(f, mbr, 0))
                return false;
            if (mp.partIndex < 0 || mp.partIndex >= 4)
                return false;
            partStart = mbr.Partitions[mp.partIndex].Start;
            partSize = mbr.Partitions[mp.partIndex].Size;
        }
        return true;
    }

    // Parte "/home/user/file.txt" en {"home","user","file.txt"}
    static std::vector<std::string> SplitPath(const std::string &path)
    {
        std::vector<std::string> parts;
        std::istringstream ss(path);
        std::string tok;
        while (std::getline(ss, tok, '/'))
            if (!tok.empty())
                parts.push_back(tok);
        return parts;
    }

    static bool ValidatePathExt2(const std::string &path, std::string &err)
    {
        if (path.empty() || path[0] != '/')
        {
            err = "la ruta debe ser absoluta";
            return false;
        }

        auto parts = SplitPath(path);
        if (parts.empty())
        {
            err = "ruta invalida";
            return false;
        }

        for (const auto &p : parts)
        {
            if (p == "." || p == "..")
            {
                err = "la ruta no permite componentes '.' o '..'";
                return false;
            }
            if (p.size() > 12)
            {
                err = "nombre excede 12 caracteres: " + p;
                return false;
            }
        }

        return true;
    }

    // Busca la entrada `name` en el directorio cuyo inodo tiene indice dirInoIdx.
    // Retorna el indice del inodo de la entrada, o -1 si no existe.
    static int FindInDir(std::fstream &f, const SuperBloque &sb,
                         int dirInoIdx, const std::string &name)
    {
        int inoSize = (int)sizeof(Inodo);

        Inodo dino{};
        if (!Utilities::ReadObject(f, dino, sb.s_inode_start + dirInoIdx * inoSize))
            return -1;
        if (dino.i_type != '0')
            return -1;

        std::vector<int> dirBlocks;
        Utilities::CollectInodeBlockIndices(f, sb, dino, dirBlocks, nullptr);

        for (int blkIdx : dirBlocks)
        {
            BloqueDir bd{};
            if (!Utilities::ReadObject(f, bd, sb.s_block_start + blkIdx * (int)sizeof(BloqueDir)))
                continue;
            for (int j = 0; j < 4; j++)
            {
                if (bd.b_content[j].b_inodo == -1)
                    continue;
                if (std::strncmp(bd.b_content[j].b_name, name.c_str(), 12) == 0)
                    return bd.b_content[j].b_inodo;
            }
        }

        return -1;
    }

    static int FindFirstFreeBitmapIndex(std::fstream &f, int bmStart, int count, int hint)
    {
        if (count <= 0)
            return -1;

        if (hint < 0 || hint >= count)
            hint = 0;

        auto scanRange = [&](int from, int to) -> int
        {
            for (int i = from; i < to; i++)
            {
                char bit = 0;
                f.seekg(bmStart + i, std::ios::beg);
                f.read(&bit, 1);
                if (!bit)
                    return i;
            }
            return -1;
        };

        int idx = scanRange(hint, count);
        if (idx != -1)
            return idx;
        return scanRange(0, hint);
    }

    // Aloca el primer inodo libre en el bitmap y actualiza el SuperBloque en disco.
    // Retorna el indice del inodo alocado, o -1 si no hay espacio.
    static int AllocInode(std::fstream &f, SuperBloque &sb, int partStart)
    {
        if (sb.s_free_inodes_count <= 0)
            return -1;

        int idx = FindFirstFreeBitmapIndex(f, sb.s_bm_inode_start, sb.s_inodes_count, sb.s_firts_ino);
        if (idx == -1)
            return -1;

        char bit = 1;
        f.seekp(sb.s_bm_inode_start + idx, std::ios::beg);
        f.write(&bit, 1);

        sb.s_free_inodes_count--;
        sb.s_firts_ino = (idx + 1 < sb.s_inodes_count) ? (idx + 1) : 0;
        Utilities::WriteObject(f, sb, partStart);
        return idx;
    }

    // Aloca el primer bloque libre en el bitmap y actualiza el SuperBloque en disco.
    // Retorna el indice del bloque alocado, o -1 si no hay espacio.
    static int AllocBlock(std::fstream &f, SuperBloque &sb, int partStart)
    {
        if (sb.s_free_blocks_count <= 0)
            return -1;

        int idx = FindFirstFreeBitmapIndex(f, sb.s_bm_block_start, sb.s_blocks_count, sb.s_first_blo);
        if (idx == -1)
            return -1;

        char bit = 1;
        f.seekp(sb.s_bm_block_start + idx, std::ios::beg);
        f.write(&bit, 1);

        sb.s_free_blocks_count--;
        sb.s_first_blo = (idx + 1 < sb.s_blocks_count) ? (idx + 1) : 0;
        Utilities::WriteObject(f, sb, partStart);
        return idx;
    }

    static int CeilDiv(int a, int b)
    {
        return (a + b - 1) / b;
    }

    static void SetName12(char dest[12], const std::string &name)
    {
        std::memset(dest, 0, 12);
        size_t n = std::min<size_t>(12, name.size());
        if (n > 0)
            std::memcpy(dest, name.c_str(), n);
    }

    static std::string NameFrom12(const char name[12])
    {
        return std::string(name, strnlen(name, 12));
    }

    static int MaxFileDataBlocks()
    {
        return 12 + 16 + (16 * 16) + (16 * 16 * 16);
    }

    static int CountPointerBlocksForDataBlocks(int dataBlockCount)
    {
        int remaining = std::max(0, dataBlockCount - 12);
        int ptrCount = 0;

        int simpleData = std::min(remaining, 16);
        if (simpleData > 0)
            ptrCount += 1;
        remaining -= simpleData;

        int doubleData = std::min(remaining, 16 * 16);
        if (doubleData > 0)
            ptrCount += 1 + CeilDiv(doubleData, 16);
        remaining -= doubleData;

        int tripleData = std::min(remaining, 16 * 16 * 16);
        if (tripleData > 0)
            ptrCount += 1 + CeilDiv(tripleData, 16 * 16) + CeilDiv(tripleData, 16);

        return ptrCount;
    }

    static void InitPointerBlock(BloqueApunt &ap)
    {
        for (int i = 0; i < 16; i++)
            ap.b_pointers[i] = -1;
    }

    static void InitDirBlock(BloqueDir &bd)
    {
        for (int i = 0; i < 4; i++)
        {
            bd.b_content[i].b_inodo = -1;
            std::memset(bd.b_content[i].b_name, 0, sizeof(bd.b_content[i].b_name));
        }
    }

    static bool TryInsertInDirBlock(std::fstream &f, const SuperBloque &sb,
                                    int blkIdx, const std::string &entryName, int newInoIdx)
    {
        BloqueDir bd{};
        int pos = sb.s_block_start + blkIdx * (int)sizeof(BloqueDir);
        if (!Utilities::ReadObject(f, bd, pos))
            return false;

        for (int i = 0; i < 4; i++)
        {
            if (bd.b_content[i].b_inodo != -1)
                continue;
            SetName12(bd.b_content[i].b_name, entryName);
            bd.b_content[i].b_inodo = newInoIdx;
            return Utilities::WriteObject(f, bd, pos);
        }
        return false;
    }

    static int AllocDirDataBlock(std::fstream &f, SuperBloque &sb, int partStart,
                                 const std::string &entryName, int newInoIdx)
    {
        int newBlk = AllocBlock(f, sb, partStart);
        if (newBlk == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        BloqueDir bd{};
        InitDirBlock(bd);
        SetName12(bd.b_content[0].b_name, entryName);
        bd.b_content[0].b_inodo = newInoIdx;
        if (!Utilities::WriteObject(f, bd, sb.s_block_start + newBlk * (int)sizeof(BloqueDir)))
            return -1;
        return newBlk;
    }

    static int AllocPointerDataBlock(std::fstream &f, SuperBloque &sb, int partStart)
    {
        int blk = AllocBlock(f, sb, partStart);
        if (blk == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        BloqueApunt ap{};
        InitPointerBlock(ap);
        if (!Utilities::WriteObject(f, ap, sb.s_block_start + blk * (int)sizeof(BloqueApunt)))
            return -1;
        return blk;
    }

    static bool InsertDirEntryRecursive(std::fstream &f, SuperBloque &sb, int partStart,
                                        int ptrBlkIdx, int level,
                                        const std::string &entryName, int newInoIdx,
                                        bool &allocatedNewDir)
    {
        if (ptrBlkIdx < 0 || level <= 0)
            return false;

        BloqueApunt ap{};
        int apPos = sb.s_block_start + ptrBlkIdx * (int)sizeof(BloqueApunt);
        if (!Utilities::ReadObject(f, ap, apPos))
            return false;

        // Primero intentar reusar bloques ya enlazados.
        for (int i = 0; i < 16; i++)
        {
            int child = ap.b_pointers[i];
            if (child == -1)
                continue;

            if (level == 1)
            {
                if (TryInsertInDirBlock(f, sb, child, entryName, newInoIdx))
                    return true;
            }
            else
            {
                if (InsertDirEntryRecursive(f, sb, partStart, child, level - 1,
                                            entryName, newInoIdx, allocatedNewDir))
                    return true;
            }
        }

        // Si no hay espacio, intentar enlazar nuevo hijo en un puntero libre.
        for (int i = 0; i < 16; i++)
        {
            if (ap.b_pointers[i] != -1)
                continue;

            if (level == 1)
            {
                int newDirBlk = AllocDirDataBlock(f, sb, partStart, entryName, newInoIdx);
                if (newDirBlk == -1)
                    return false;
                ap.b_pointers[i] = newDirBlk;
                if (!Utilities::WriteObject(f, ap, apPos))
                    return false;
                allocatedNewDir = true;
                return true;
            }

            int newPtrBlk = AllocPointerDataBlock(f, sb, partStart);
            if (newPtrBlk == -1)
                return false;
            ap.b_pointers[i] = newPtrBlk;
            if (!Utilities::WriteObject(f, ap, apPos))
                return false;

            if (InsertDirEntryRecursive(f, sb, partStart, newPtrBlk, level - 1,
                                        entryName, newInoIdx, allocatedNewDir))
                return true;

            return false;
        }

        return false;
    }

    // Agrega la entrada (entryName -> newInoIdx) al directorio cuyo inodo es dirInoIdx.
    // Busca un slot libre en los bloques existentes; si no hay, aloca un nuevo bloque.
    static bool AddDirEntry(std::fstream &f, SuperBloque &sb, int partStart,
                            int dirInoIdx, const std::string &entryName, int newInoIdx)
    {
        int inoSize = (int)sizeof(Inodo);

        Inodo dino{};
        int dinoPos = sb.s_inode_start + dirInoIdx * inoSize;
        if (!Utilities::ReadObject(f, dino, dinoPos))
            return false;

        // Intentar insertar en cualquier bloque ya existente (directo/indirecto).
        std::vector<int> existingBlocks;
        Utilities::CollectInodeBlockIndices(f, sb, dino, existingBlocks, nullptr);
        for (int blkIdx : existingBlocks)
        {
            if (TryInsertInDirBlock(f, sb, blkIdx, entryName, newInoIdx))
                return true;
        }

        // Luego intentar crecer directos.
        for (int i = 0; i < 12; i++)
        {
            if (dino.i_block[i] != -1)
                continue;
            int newBlk = AllocDirDataBlock(f, sb, partStart, entryName, newInoIdx);
            if (newBlk == -1)
                return false;
            dino.i_block[i] = newBlk;
            dino.i_size += (int)sizeof(BloqueDir);
            return Utilities::WriteObject(f, dino, dinoPos);
        }

        // Si directos llenos, crecer por simple/doble/triple.
        for (int idx = 12; idx <= 14; idx++)
        {
            if (dino.i_block[idx] == -1)
            {
                int ptrBlk = AllocPointerDataBlock(f, sb, partStart);
                if (ptrBlk == -1)
                    return false;
                dino.i_block[idx] = ptrBlk;
                if (!Utilities::WriteObject(f, dino, dinoPos))
                    return false;
            }

            bool allocatedNewDir = false;
            if (InsertDirEntryRecursive(f, sb, partStart, dino.i_block[idx], idx - 11,
                                        entryName, newInoIdx, allocatedNewDir))
            {
                if (allocatedNewDir)
                {
                    dino.i_size += (int)sizeof(BloqueDir);
                    Utilities::WriteObject(f, dino, dinoPos);
                }
                return true;
            }
        }

        return false;
    }

    // Lee el contenido completo de un archivo dado su inodo.
    static std::string ReadFileContent(std::fstream &f, const SuperBloque &sb, const Inodo &ino)
    {
        return Utilities::ReadFileData(f, sb, ino);
    }

    // Verifica permiso de lectura (bit 4) para el usuario actual sobre el inodo.
    static bool CanRead(const Inodo &ino, int uid, int gid)
    {
        if (uid == 1)
            return true; // root
        int o = (ino.i_uid == uid) ? 0 : (ino.i_gid == gid) ? 1
                                                            : 2;
        int p = ino.i_perm[o] - '0';
        return (p & 4) != 0;
    }

    // Verifica permiso de escritura (bit 2) para el usuario actual sobre el inodo.
    static bool CanWrite(const Inodo &ino, int uid, int gid)
    {
        if (uid == 1)
            return true; // root
        int o = (ino.i_uid == uid) ? 0 : (ino.i_gid == gid) ? 1
                                                            : 2;
        int p = ino.i_perm[o] - '0';
        return (p & 2) != 0;
    }

    // Verifica permiso de ejecucion (bit 1), usado para atravesar directorios.
    static bool CanExec(const Inodo &ino, int uid, int gid)
    {
        if (uid == 1)
            return true; // root
        int o = (ino.i_uid == uid) ? 0 : (ino.i_gid == gid) ? 1
                                                            : 2;
        int p = ino.i_perm[o] - '0';
        return (p & 1) != 0;
    }

    // Recorre una ruta aplicando permisos de traversal sobre directorios.
    // Retorna -1 si no existe algun componente, -2 en errores de permisos/tipo.
    static int TraversePathWithPerm(std::fstream &f, const SuperBloque &sb,
                                    const std::string &path, bool upToParent,
                                    std::string &lastName,
                                    int uid, int gid,
                                    std::string &err)
    {
        auto parts = SplitPath(path);
        if (parts.empty())
            return 1;

        int curIno = 1;
        int limit = upToParent ? (int)parts.size() - 1 : (int)parts.size();

        for (int i = 0; i < limit; i++)
        {
            Inodo current{};
            if (!Utilities::ReadObject(f, current, sb.s_inode_start + curIno * (int)sizeof(Inodo)))
            {
                err = "no se pudo leer inodo durante traversal";
                return -2;
            }
            if (current.i_type != '0')
            {
                err = "la ruta contiene un archivo intermedio";
                return -2;
            }
            if (!CanExec(current, uid, gid))
            {
                err = "permiso denegado para atravesar directorio";
                return -2;
            }

            int found = FindInDir(f, sb, curIno, parts[i]);
            if (found == -1)
                return -1;
            curIno = found;
        }

        if (upToParent && !parts.empty())
            lastName = parts.back();
        return curIno;
    }

    // Crea el nodo de directorio (inodo + BloqueDir con "." y ".."), retorna inodo index.
    static int CreateDirNode(std::fstream &f, SuperBloque &sb, int partStart,
                             int parentInoIdx, int uid, int gid)
    {
        int newIno = AllocInode(f, sb, partStart);
        if (newIno == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        int newBlk = AllocBlock(f, sb, partStart);
        if (newBlk == -1)
            return -1;
        Utilities::ReadObject(f, sb, partStart);

        int64_t now = (int64_t)std::time(nullptr);
        Inodo ino{};
        ino.i_uid = uid;
        ino.i_gid = gid;
        ino.i_size = (int32_t)sizeof(BloqueDir);
        ino.i_atime = ino.i_ctime = ino.i_mtime = now;
        for (int k = 0; k < 15; k++)
            ino.i_block[k] = -1;
        ino.i_block[0] = newBlk;
        ino.i_type = '0';
        std::memcpy(ino.i_perm, "664", 3);
        Utilities::WriteObject(f, ino, sb.s_inode_start + newIno * (int)sizeof(Inodo));

        BloqueDir bd{};
        for (int k = 0; k < 4; k++)
        {
            bd.b_content[k].b_inodo = -1;
            bd.b_content[k].b_name[0] = '\0';
        }
        SetName12(bd.b_content[0].b_name, ".");
        bd.b_content[0].b_inodo = newIno;
        SetName12(bd.b_content[1].b_name, "..");
        bd.b_content[1].b_inodo = parentInoIdx;
        Utilities::WriteObject(f, bd, sb.s_block_start + newBlk * (int)sizeof(BloqueDir));

        return newIno;
    }

    // ================================================================
    //  Helpers para obtener la particion activa
    // ================================================================

    static bool GetActivePartition(const DiskManagement::MountedPartition *&mpOut,
                                   std::string &errOut)
    {
        if (!UserSession::IsLoggedIn())
        {
            errOut = "no hay sesion activa";
            return false;
        }
        auto it = DiskManagement::MountMap.find(UserSession::GetCurrentId());
        if (it == DiskManagement::MountMap.end())
        {
            errOut = "particion no encontrada: " + UserSession::GetCurrentId();
            return false;
        }
        mpOut = &it->second;
        return true;
    }

    // ================================================================
    //  MKDIR
    // ================================================================

    std::string Mkdir(const std::string &path, bool parents)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [mkdir]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [mkdir]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [mkdir]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [mkdir]: particion no formateada con EXT2";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();

        auto parts = SplitPath(path);
        if (parts.empty())
        {
            f.close();
            return "Error [mkdir]: ruta invalida";
        }

        std::string pathErr;
        if (!ValidatePathExt2(path, pathErr))
        {
            f.close();
            return "Error [mkdir]: " + pathErr;
        }

        int curIno = 1; // inodo raiz
        for (int i = 0; i < (int)parts.size(); i++)
        {
            Inodo currentIno{};
            if (!Utilities::ReadObject(f, currentIno, sb.s_inode_start + curIno * (int)sizeof(Inodo)))
            {
                f.close();
                return "Error [mkdir]: no se pudo leer inodo del directorio actual";
            }
            if (currentIno.i_type != '0')
            {
                f.close();
                return "Error [mkdir]: la ruta contiene un archivo intermedio";
            }
            if (!CanExec(currentIno, uid, gid))
            {
                f.close();
                return "Error [mkdir]: permiso denegado para atravesar directorio";
            }

            int found = FindInDir(f, sb, curIno, parts[i]);
            if (found == -1)
            {
                // El componente no existe: solo se puede crear si parents=true o es el ultimo
                if (i < (int)parts.size() - 1 && !parents)
                {
                    f.close();
                    return "Error [mkdir]: directorio padre no existe: " + parts[i] + " (usa -p)";
                }

                // Verificar permiso de escritura en el directorio actual
                if (!CanWrite(currentIno, uid, gid) || !CanExec(currentIno, uid, gid))
                {
                    f.close();
                    return "Error [mkdir]: permiso denegado en directorio: /" + parts[i];
                }

                int newIno = CreateDirNode(f, sb, partStart, curIno, uid, gid);
                if (newIno == -1)
                {
                    f.close();
                    return "Error [mkdir]: sin espacio disponible";
                }

                // Re-leer sb luego de las alocaciones
                Utilities::ReadObject(f, sb, partStart);

                if (!AddDirEntry(f, sb, partStart, curIno, parts[i], newIno))
                {
                    f.close();
                    return "Error [mkdir]: no se pudo agregar entrada al directorio padre";
                }
                Utilities::ReadObject(f, sb, partStart);
                curIno = newIno;
            }
            else
            {
                Inodo foundIno{};
                if (!Utilities::ReadObject(f, foundIno, sb.s_inode_start + found * (int)sizeof(Inodo)))
                {
                    f.close();
                    return "Error [mkdir]: no se pudo leer inodo existente";
                }
                if (foundIno.i_type != '0')
                {
                    f.close();
                    return "Error [mkdir]: ya existe un archivo con ese nombre en la ruta";
                }

                if (i == (int)parts.size() - 1 && !parents)
                {
                    f.close();
                    return "Error [mkdir]: el directorio ya existe: " + path;
                }
                curIno = found;
            }
        }

        f.close();
        return "OK [mkdir]: directorio creado: " + path;
    }

    // ================================================================
    //  MKFILE
    // ================================================================

    std::string Mkfile(const std::string &path, bool recursive, int size, const std::string &cont)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [mkfile]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [mkfile]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [mkfile]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [mkfile]: particion no formateada con EXT2";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();

        if (size < 0)
        {
            f.close();
            return "Error [mkfile]: -size no puede ser negativo";
        }

        std::string pathErr;
        if (!ValidatePathExt2(path, pathErr))
        {
            f.close();
            return "Error [mkfile]: " + pathErr;
        }

        // Construir contenido del archivo
        std::string fileContent;
        if (!cont.empty())
        {
            std::ifstream cf(cont, std::ios::binary);
            if (!cf.is_open())
            {
                // Si no es un path valido, usar el valor como contenido inline
                fileContent = cont;
            }
            else
            {
                fileContent.assign((std::istreambuf_iterator<char>(cf)),
                                   std::istreambuf_iterator<char>());
            }
        }
        else if (size > 0)
        {
            fileContent.resize(size);
            for (int i = 0; i < size; i++)
                fileContent[i] = '0' + (i % 10);
        }
        // Si ni cont ni size: archivo queda vacio

        // Obtener directorio padre e identificar el nombre del archivo
        std::string lastName;
        std::string travErr;
        int parentInoIdx = TraversePathWithPerm(f, sb, path, true, lastName, uid, gid, travErr);

        if (parentInoIdx == -2)
        {
            f.close();
            return "Error [mkfile]: " + travErr;
        }

        if (parentInoIdx == -1)
        {
            if (!recursive)
            {
                f.close();
                return "Error [mkfile]: directorio padre no existe (usa -r)";
            }

            // Crear todos los directorios intermedios
            auto parts = SplitPath(path);
            if (parts.empty())
            {
                f.close();
                return "Error [mkfile]: ruta invalida";
            }
            lastName = parts.back();

            int curIno = 1;
            for (int i = 0; i < (int)parts.size() - 1; i++)
            {
                Inodo curInoMeta{};
                if (!Utilities::ReadObject(f, curInoMeta, sb.s_inode_start + curIno * (int)sizeof(Inodo)) ||
                    curInoMeta.i_type != '0')
                {
                    f.close();
                    return "Error [mkfile]: la ruta padre contiene un archivo intermedio";
                }
                if (!CanExec(curInoMeta, uid, gid))
                {
                    f.close();
                    return "Error [mkfile]: permiso denegado para atravesar ruta intermedia";
                }

                int found = FindInDir(f, sb, curIno, parts[i]);
                if (found == -1)
                {
                    if (!CanWrite(curInoMeta, uid, gid) || !CanExec(curInoMeta, uid, gid))
                    {
                        f.close();
                        return "Error [mkfile]: permiso denegado en ruta intermedia";
                    }

                    int newIno = CreateDirNode(f, sb, partStart, curIno, uid, gid);
                    if (newIno == -1)
                    {
                        f.close();
                        return "Error [mkfile]: sin espacio";
                    }
                    Utilities::ReadObject(f, sb, partStart);
                    if (!AddDirEntry(f, sb, partStart, curIno, parts[i], newIno))
                    {
                        f.close();
                        return "Error [mkfile]: no se pudo crear dir: " + parts[i];
                    }
                    Utilities::ReadObject(f, sb, partStart);
                    curIno = newIno;
                }
                else
                {
                    Inodo foundIno{};
                    if (!Utilities::ReadObject(f, foundIno, sb.s_inode_start + found * (int)sizeof(Inodo)) ||
                        foundIno.i_type != '0')
                    {
                        f.close();
                        return "Error [mkfile]: la ruta padre contiene un archivo intermedio";
                    }
                    curIno = found;
                }
            }
            parentInoIdx = curIno;
        }

        // Verificar permiso de escritura en el directorio padre
        Inodo parentIno{};
        Utilities::ReadObject(f, parentIno, sb.s_inode_start + parentInoIdx * (int)sizeof(Inodo));
        if (parentIno.i_type != '0')
        {
            f.close();
            return "Error [mkfile]: el padre indicado no es un directorio";
        }
        if (!CanWrite(parentIno, uid, gid) || !CanExec(parentIno, uid, gid))
        {
            f.close();
            return "Error [mkfile]: permiso denegado en directorio padre";
        }

        // Verificar si ya existe
        if (FindInDir(f, sb, parentInoIdx, lastName) != -1)
        {
            f.close();
            return "Error [mkfile]: el archivo ya existe: " + path;
        }

        // Alocar inodo para el nuevo archivo
        int fileIno = AllocInode(f, sb, partStart);
        if (fileIno == -1)
        {
            f.close();
            return "Error [mkfile]: sin inodos libres";
        }
        Utilities::ReadObject(f, sb, partStart);

        int64_t now = (int64_t)std::time(nullptr);
        int bsize = (int)sizeof(BloqueFile);
        int totalBytes = (int)fileContent.size();
        int dataBlockCount = CeilDiv(totalBytes, bsize);

        if (dataBlockCount > MaxFileDataBlocks())
        {
            f.close();
            return "Error [mkfile]: el archivo excede la capacidad maxima del inodo";
        }

        int pointerBlockCount = CountPointerBlocksForDataBlocks(dataBlockCount);
        if (sb.s_free_blocks_count < dataBlockCount + pointerBlockCount)
        {
            f.close();
            return "Error [mkfile]: sin bloques libres suficientes";
        }

        std::vector<int> dataBlocks;
        dataBlocks.reserve(dataBlockCount);

        for (int blockNo = 0; blockNo < dataBlockCount; blockNo++)
        {
            int blkIdx = AllocBlock(f, sb, partStart);
            if (blkIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueFile bf{};
            std::memset(bf.b_content, 0, bsize);
            int offset = blockNo * bsize;
            int take = std::min(bsize, totalBytes - offset);
            if (take > 0)
                std::memcpy(bf.b_content, fileContent.c_str() + offset, take);
            Utilities::WriteObject(f, bf, sb.s_block_start + blkIdx * bsize);
            dataBlocks.push_back(blkIdx);
        }

        Inodo ino{};
        ino.i_uid = uid;
        ino.i_gid = gid;
        ino.i_size = totalBytes;
        ino.i_atime = ino.i_ctime = ino.i_mtime = now;
        for (int k = 0; k < 15; k++)
            ino.i_block[k] = -1;
        ino.i_type = '1';
        std::memcpy(ino.i_perm, "664", 3);

        int cursor = 0;
        for (int i = 0; i < 12 && cursor < (int)dataBlocks.size(); i++)
            ino.i_block[i] = dataBlocks[cursor++];

        if (cursor < (int)dataBlocks.size())
        {
            int apIdx = AllocBlock(f, sb, partStart);
            if (apIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres (apuntador simple)";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueApunt ap{};
            InitPointerBlock(ap);
            ino.i_block[12] = apIdx;
            for (int i = 0; i < 16 && cursor < (int)dataBlocks.size(); i++)
                ap.b_pointers[i] = dataBlocks[cursor++];
            Utilities::WriteObject(f, ap, sb.s_block_start + apIdx * (int)sizeof(BloqueApunt));
        }

        if (cursor < (int)dataBlocks.size())
        {
            int dblIdx = AllocBlock(f, sb, partStart);
            if (dblIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres (apuntador doble)";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueApunt dbl{};
            InitPointerBlock(dbl);
            ino.i_block[13] = dblIdx;

            for (int i = 0; i < 16 && cursor < (int)dataBlocks.size(); i++)
            {
                int lvl1Idx = AllocBlock(f, sb, partStart);
                if (lvl1Idx == -1)
                {
                    f.close();
                    return "Error [mkfile]: sin bloques libres (nivel doble)";
                }
                Utilities::ReadObject(f, sb, partStart);

                BloqueApunt lvl1{};
                InitPointerBlock(lvl1);
                dbl.b_pointers[i] = lvl1Idx;

                for (int j = 0; j < 16 && cursor < (int)dataBlocks.size(); j++)
                    lvl1.b_pointers[j] = dataBlocks[cursor++];

                Utilities::WriteObject(f, lvl1, sb.s_block_start + lvl1Idx * (int)sizeof(BloqueApunt));
            }

            Utilities::WriteObject(f, dbl, sb.s_block_start + dblIdx * (int)sizeof(BloqueApunt));
        }

        if (cursor < (int)dataBlocks.size())
        {
            int triIdx = AllocBlock(f, sb, partStart);
            if (triIdx == -1)
            {
                f.close();
                return "Error [mkfile]: sin bloques libres (apuntador triple)";
            }
            Utilities::ReadObject(f, sb, partStart);

            BloqueApunt tri{};
            InitPointerBlock(tri);
            ino.i_block[14] = triIdx;

            for (int i = 0; i < 16 && cursor < (int)dataBlocks.size(); i++)
            {
                int lvl2Idx = AllocBlock(f, sb, partStart);
                if (lvl2Idx == -1)
                {
                    f.close();
                    return "Error [mkfile]: sin bloques libres (nivel triple 1)";
                }
                Utilities::ReadObject(f, sb, partStart);

                BloqueApunt lvl2{};
                InitPointerBlock(lvl2);
                tri.b_pointers[i] = lvl2Idx;

                for (int j = 0; j < 16 && cursor < (int)dataBlocks.size(); j++)
                {
                    int lvl1Idx = AllocBlock(f, sb, partStart);
                    if (lvl1Idx == -1)
                    {
                        f.close();
                        return "Error [mkfile]: sin bloques libres (nivel triple 2)";
                    }
                    Utilities::ReadObject(f, sb, partStart);

                    BloqueApunt lvl1{};
                    InitPointerBlock(lvl1);
                    lvl2.b_pointers[j] = lvl1Idx;

                    for (int k = 0; k < 16 && cursor < (int)dataBlocks.size(); k++)
                        lvl1.b_pointers[k] = dataBlocks[cursor++];

                    Utilities::WriteObject(f, lvl1, sb.s_block_start + lvl1Idx * (int)sizeof(BloqueApunt));
                }

                Utilities::WriteObject(f, lvl2, sb.s_block_start + lvl2Idx * (int)sizeof(BloqueApunt));
            }

            Utilities::WriteObject(f, tri, sb.s_block_start + triIdx * (int)sizeof(BloqueApunt));
        }

        // Escribir el inodo del archivo
        Utilities::WriteObject(f, ino, sb.s_inode_start + fileIno * (int)sizeof(Inodo));

        // Insertar entrada en el directorio padre
        if (!AddDirEntry(f, sb, partStart, parentInoIdx, lastName, fileIno))
        {
            f.close();
            return "Error [mkfile]: no se pudo agregar entrada al directorio padre";
        }

        f.close();
        return "OK [mkfile]: archivo creado: " + path;
    }

    std::string Edit(const std::string &path, const std::string &cont)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [edit]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [edit]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        (void)partSize;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [edit]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [edit]: particion no formateada";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();
        std::string name, travErr;
        int parentIno = TraversePathWithPerm(f, sb, path, true, name, uid, gid, travErr);
        if (parentIno < 0)
        {
            f.close();
            return "Error [edit]: ruta no encontrada";
        }

        Inodo parent{};
        if (!Utilities::ReadObject(f, parent, sb.s_inode_start + parentIno * (int)sizeof(Inodo)) || !CanWrite(parent, uid, gid))
        {
            f.close();
            return "Error [edit]: permiso denegado";
        }

        int targetIno = FindInDir(f, sb, parentIno, name);
        if (targetIno == -1)
        {
            f.close();
            return "Error [edit]: archivo no encontrado";
        }

        Inodo target{};
        if (!Utilities::ReadObject(f, target, sb.s_inode_start + targetIno * (int)sizeof(Inodo)))
        {
            f.close();
            return "Error [edit]: no se pudo leer el archivo";
        }
        if (target.i_type != '1')
        {
            f.close();
            return "Error [edit]: la ruta no corresponde a un archivo";
        }

        f.close();

        std::string rm = Remove(path);
        if (rm.rfind("Error", 0) == 0)
            return "Error [edit]: " + rm;

        std::string mk = Mkfile(path, true, 0, cont);
        if (mk.rfind("Error", 0) == 0)
            return "Error [edit]: " + mk;

        return "OK [edit]: archivo actualizado: " + path;
    }

    // ================================================================
    //  CAT
    // ================================================================

    std::string Cat(const std::vector<std::string> &files)
    {
        if (files.empty())
            return "Error [cat]: no se especificaron archivos";

        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [cat]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [cat]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [cat]: no se pudo leer limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [cat]: particion no formateada con EXT2";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();

        std::ostringstream output;
        for (const auto &filePath : files)
        {
            std::string pathErr;
            if (!ValidatePathExt2(filePath, pathErr))
            {
                output << "Error [cat]: " << pathErr << ": " << filePath << "\n";
                continue;
            }

            std::string dummy;
            std::string travErr;
            int inoIdx = TraversePathWithPerm(f, sb, filePath, false, dummy, uid, gid, travErr);
            if (inoIdx == -2)
            {
                output << "Error [cat]: " << travErr << ": " << filePath << "\n";
                continue;
            }
            if (inoIdx == -1)
            {
                output << "Error [cat]: archivo no encontrado: " << filePath << "\n";
                continue;
            }

            Inodo ino{};
            if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
            {
                output << "Error [cat]: no se pudo leer inodo de: " << filePath << "\n";
                continue;
            }

            if (ino.i_type != '1')
            {
                output << "Error [cat]: " << filePath << " es un directorio\n";
                continue;
            }

            if (!CanRead(ino, uid, gid))
            {
                output << "Error [cat]: permiso denegado: " << filePath << "\n";
                continue;
            }

            output << ReadFileContent(f, sb, ino);
        }

        f.close();
        return output.str();
    }

    static bool FindDirEntrySlot(std::fstream &f, const SuperBloque &sb, int dirInoIdx,
                                 const std::string &name, int &blkIdxOut, int &slotOut)
    {
        Inodo dino{};
        if (!Utilities::ReadObject(f, dino, sb.s_inode_start + dirInoIdx * (int)sizeof(Inodo)) || dino.i_type != '0')
            return false;

        std::vector<int> dirBlocks;
        Utilities::CollectInodeBlockIndices(f, sb, dino, dirBlocks, nullptr);

        for (int blkIdx : dirBlocks)
        {
            BloqueDir bd{};
            int pos = sb.s_block_start + blkIdx * (int)sizeof(BloqueDir);
            if (!Utilities::ReadObject(f, bd, pos))
                continue;
            for (int i = 0; i < 4; i++)
            {
                if (bd.b_content[i].b_inodo == -1)
                    continue;
                if (std::strncmp(bd.b_content[i].b_name, name.c_str(), 12) == 0)
                {
                    blkIdxOut = blkIdx;
                    slotOut = i;
                    return true;
                }
            }
        }
        return false;
    }

    static std::vector<std::pair<std::string, int>> ListDirEntries(std::fstream &f, const SuperBloque &sb, int dirInoIdx)
    {
        std::vector<std::pair<std::string, int>> out;
        Inodo dino{};
        if (!Utilities::ReadObject(f, dino, sb.s_inode_start + dirInoIdx * (int)sizeof(Inodo)) || dino.i_type != '0')
            return out;

        std::vector<int> dirBlocks;
        Utilities::CollectInodeBlockIndices(f, sb, dino, dirBlocks, nullptr);
        for (int blkIdx : dirBlocks)
        {
            BloqueDir bd{};
            if (!Utilities::ReadObject(f, bd, sb.s_block_start + blkIdx * (int)sizeof(BloqueDir)))
                continue;
            for (int i = 0; i < 4; i++)
            {
                if (bd.b_content[i].b_inodo == -1)
                    continue;
                std::string nm = NameFrom12(bd.b_content[i].b_name);
                if (nm == "." || nm == "..")
                    continue;
                out.push_back({nm, bd.b_content[i].b_inodo});
            }
        }
        return out;
    }

    static void MarkInodeFree(std::fstream &f, SuperBloque &sb, int idx)
    {
        if (idx < 0 || idx >= sb.s_inodes_count)
            return;
        char bit = 0;
        f.seekp(sb.s_bm_inode_start + idx, std::ios::beg);
        f.write(&bit, 1);
        sb.s_free_inodes_count++;
        if (idx < sb.s_firts_ino || sb.s_firts_ino <= 0)
            sb.s_firts_ino = idx;
    }

    static void MarkBlockFree(std::fstream &f, SuperBloque &sb, int idx)
    {
        if (idx < 0 || idx >= sb.s_blocks_count)
            return;
        char bit = 0;
        f.seekp(sb.s_bm_block_start + idx, std::ios::beg);
        f.write(&bit, 1);
        sb.s_free_blocks_count++;
        if (idx < sb.s_first_blo || sb.s_first_blo <= 0)
            sb.s_first_blo = idx;
    }

    static bool DeleteSubtree(std::fstream &f, SuperBloque &sb, int inoIdx, std::set<int> &visitedIno)
    {
        if (inoIdx < 0 || inoIdx >= sb.s_inodes_count)
            return false;
        if (visitedIno.count(inoIdx))
            return true;
        visitedIno.insert(inoIdx);

        Inodo ino{};
        if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
            return false;

        if (ino.i_type == '0')
        {
            auto children = ListDirEntries(f, sb, inoIdx);
            for (const auto &child : children)
            {
                if (!DeleteSubtree(f, sb, child.second, visitedIno))
                    return false;
            }
        }

        std::vector<int> dataBlocks;
        std::vector<int> ptrBlocks;
        Utilities::CollectInodeBlockIndices(f, sb, ino, dataBlocks, &ptrBlocks);

        for (int b : dataBlocks)
            MarkBlockFree(f, sb, b);
        for (int b : ptrBlocks)
            MarkBlockFree(f, sb, b);

        Inodo empty{};
        for (int k = 0; k < 15; k++)
            empty.i_block[k] = -1;
        Utilities::WriteObject(f, empty, sb.s_inode_start + inoIdx * (int)sizeof(Inodo));
        MarkInodeFree(f, sb, inoIdx);
        return true;
    }

    std::string Remove(const std::string &path, bool recursive)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [remove]: " + err;

        if (path == "/")
            return "Error [remove]: no se permite eliminar la raiz";

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [remove]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        (void)partSize;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [remove]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [remove]: particion no formateada";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();

        std::string name;
        std::string travErr;
        int parentInoIdx = TraversePathWithPerm(f, sb, path, true, name, uid, gid, travErr);
        if (parentInoIdx < 0)
        {
            f.close();
            return "Error [remove]: ruta no encontrada";
        }

        Inodo parent{};
        if (!Utilities::ReadObject(f, parent, sb.s_inode_start + parentInoIdx * (int)sizeof(Inodo)))
        {
            f.close();
            return "Error [remove]: no se pudo leer inodo padre";
        }
        if (!CanWrite(parent, uid, gid))
        {
            f.close();
            return "Error [remove]: permiso denegado";
        }

        int targetIno = FindInDir(f, sb, parentInoIdx, name);
        if (targetIno == -1)
        {
            f.close();
            return "Error [remove]: ruta no encontrada";
        }

        Inodo target{};
        if (!Utilities::ReadObject(f, target, sb.s_inode_start + targetIno * (int)sizeof(Inodo)))
        {
            f.close();
            return "Error [remove]: no se pudo leer inodo objetivo";
        }
        if (target.i_type == '0' && !recursive && !ListDirEntries(f, sb, targetIno).empty())
        {
            f.close();
            return "Error [remove]: carpeta no vacia (usa -r)";
        }

        int blkIdx = -1, slot = -1;
        if (!FindDirEntrySlot(f, sb, parentInoIdx, name, blkIdx, slot))
        {
            f.close();
            return "Error [remove]: no se pudo ubicar entrada en directorio";
        }

        std::set<int> visitedIno;
        if (!DeleteSubtree(f, sb, targetIno, visitedIno))
        {
            f.close();
            return "Error [remove]: fallo al eliminar contenido";
        }

        BloqueDir bd{};
        int bpos = sb.s_block_start + blkIdx * (int)sizeof(BloqueDir);
        if (Utilities::ReadObject(f, bd, bpos))
        {
            bd.b_content[slot].b_inodo = -1;
            std::memset(bd.b_content[slot].b_name, 0, sizeof(bd.b_content[slot].b_name));
            Utilities::WriteObject(f, bd, bpos);
        }

        Utilities::WriteObject(f, sb, partStart);
        f.close();
        return "OK [remove]: eliminado: " + path;
    }

    std::string Rename(const std::string &path, const std::string &name)
    {
        if (name.empty() || name.size() > 12 || name == "." || name == "..")
            return "Error [rename]: nombre invalido";

        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [rename]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [rename]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        (void)partSize;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [rename]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [rename]: particion no formateada";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();
        std::string lastName, travErr;
        int parentInoIdx = TraversePathWithPerm(f, sb, path, true, lastName, uid, gid, travErr);
        if (parentInoIdx < 0)
        {
            f.close();
            return "Error [rename]: ruta no encontrada";
        }

        Inodo parent{};
        if (!Utilities::ReadObject(f, parent, sb.s_inode_start + parentInoIdx * (int)sizeof(Inodo)) || !CanWrite(parent, uid, gid))
        {
            f.close();
            return "Error [rename]: permiso denegado";
        }

        if (FindInDir(f, sb, parentInoIdx, name) != -1)
        {
            f.close();
            return "Error [rename]: ya existe un elemento con el nuevo nombre";
        }

        int blkIdx = -1, slot = -1;
        if (!FindDirEntrySlot(f, sb, parentInoIdx, lastName, blkIdx, slot))
        {
            f.close();
            return "Error [rename]: no se encontro la entrada";
        }

        BloqueDir bd{};
        int bpos = sb.s_block_start + blkIdx * (int)sizeof(BloqueDir);
        if (!Utilities::ReadObject(f, bd, bpos))
        {
            f.close();
            return "Error [rename]: no se pudo leer bloque de directorio";
        }
        SetName12(bd.b_content[slot].b_name, name);
        Utilities::WriteObject(f, bd, bpos);
        f.close();
        return "OK [rename]: " + path + " -> " + name;
    }

    static bool GlobMatchSimple(const std::string &text, const std::string &pattern)
    {
        if (pattern == "*")
            return true;
        if (pattern.find('*') == std::string::npos)
            return text == pattern;

        size_t pos = 0;
        size_t i = 0;
        while (i < pattern.size())
        {
            size_t star = pattern.find('*', i);
            std::string part = pattern.substr(i, (star == std::string::npos ? pattern.size() : star) - i);
            if (!part.empty())
            {
                size_t found = text.find(part, pos);
                if (found == std::string::npos)
                    return false;
                pos = found + part.size();
            }
            if (star == std::string::npos)
                break;
            i = star + 1;
        }
        return true;
    }

    std::string Find(const std::string &path, const std::string &name)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [find]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [find]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        (void)partSize;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [find]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [find]: particion no formateada";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();
        std::string dummy, travErr;
        int startIno = (path == "/") ? 1 : TraversePathWithPerm(f, sb, path, false, dummy, uid, gid, travErr);
        if (startIno < 0)
        {
            f.close();
            return "Error [find]: ruta no encontrada";
        }

        std::ostringstream out;
        std::queue<std::pair<int, std::string>> q;
        std::set<int> visited;
        q.push({startIno, path});

        while (!q.empty())
        {
            auto cur = q.front();
            q.pop();
            if (visited.count(cur.first))
                continue;
            visited.insert(cur.first);

            Inodo ino{};
            if (!Utilities::ReadObject(f, ino, sb.s_inode_start + cur.first * (int)sizeof(Inodo)))
                continue;

            std::string baseName = (cur.second == "/") ? "/" : cur.second.substr(cur.second.find_last_of('/') + 1);
            if (GlobMatchSimple(baseName, name))
                out << cur.second << "\n";

            if (ino.i_type != '0')
                continue;

            auto children = ListDirEntries(f, sb, cur.first);
            for (const auto &child : children)
            {
                std::string childPath = (cur.second == "/") ? ("/" + child.first) : (cur.second + "/" + child.first);
                q.push({child.second, childPath});
            }
        }

        f.close();
        std::string res = out.str();
        if (res.empty())
            return "(sin coincidencias)";
        return res;
    }

    std::string Copy(const std::string &path, const std::string &destino)
    {
        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [copy]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [copy]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        (void)partSize;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [copy]: no se pudo obtener limites de particion";
        }

        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [copy]: particion no formateada";
        }

        int uid = UserSession::GetCurrentUid();
        int gid = UserSession::GetCurrentGid();
        std::string dummy, travErr;
        int srcIno = TraversePathWithPerm(f, sb, path, false, dummy, uid, gid, travErr);
        if (srcIno < 0)
        {
            f.close();
            return "Error [copy]: ruta origen no encontrada";
        }

        Inodo src{};
        if (!Utilities::ReadObject(f, src, sb.s_inode_start + srcIno * (int)sizeof(Inodo)))
        {
            f.close();
            return "Error [copy]: no se pudo leer inodo origen";
        }

        std::string srcName = path.substr(path.find_last_of('/') + 1);
        std::string destPath = destino;
        int dstIno = (destino == "/") ? 1 : TraversePathWithPerm(f, sb, destino, false, dummy, uid, gid, travErr);
        if (dstIno >= 0)
        {
            Inodo dstMeta{};
            if (Utilities::ReadObject(f, dstMeta, sb.s_inode_start + dstIno * (int)sizeof(Inodo)) && dstMeta.i_type == '0')
                destPath = (destino == "/") ? ("/" + srcName) : (destino + "/" + srcName);
        }
        f.close();

        if (src.i_type == '1')
        {
            std::fstream rf = Utilities::OpenFile(mp->diskPath);
            if (!rf.is_open())
                return "Error [copy]: no se pudo abrir disco para lectura";
            Utilities::ReadObject(rf, sb, partStart);
            Utilities::ReadObject(rf, src, sb.s_inode_start + srcIno * (int)sizeof(Inodo));
            std::string content = ReadFileContent(rf, sb, src);
            rf.close();
            std::string mk = Mkfile(destPath, false, 0, content);
            if (mk.rfind("Error", 0) == 0)
                return "Error [copy]: " + mk;
            return "OK [copy]: " + path + " -> " + destPath;
        }

        std::queue<std::pair<std::string, int>> q;
        q.push({path, srcIno});
        std::string rootDest = destPath;
        std::string mk = Mkdir(rootDest, true);
        if (mk.rfind("Error", 0) == 0)
            return "Error [copy]: " + mk;

        while (!q.empty())
        {
            auto cur = q.front();
            q.pop();
            std::fstream tf = Utilities::OpenFile(mp->diskPath);
            if (!tf.is_open())
                return "Error [copy]: no se pudo reabrir disco";
            Utilities::ReadObject(tf, sb, partStart);
            auto rel = cur.first.substr(path.size());
            std::string destCur = rootDest + rel;

            auto children = ListDirEntries(tf, sb, cur.second);
            for (const auto &child : children)
            {
                Inodo cino{};
                Utilities::ReadObject(tf, cino, sb.s_inode_start + child.second * (int)sizeof(Inodo));
                std::string childSrcPath = (cur.first == "/") ? ("/" + child.first) : (cur.first + "/" + child.first);
                std::string childDstPath = (destCur == "/") ? ("/" + child.first) : (destCur + "/" + child.first);
                if (cino.i_type == '0')
                {
                    std::string r = Mkdir(childDstPath, true);
                    if (r.rfind("Error", 0) == 0)
                    {
                        tf.close();
                        return "Error [copy]: " + r;
                    }
                    q.push({childSrcPath, child.second});
                }
                else
                {
                    std::string content = ReadFileContent(tf, sb, cino);
                    std::string r = Mkfile(childDstPath, true, 0, content);
                    if (r.rfind("Error", 0) == 0)
                    {
                        tf.close();
                        return "Error [copy]: " + r;
                    }
                }
            }
            tf.close();
        }

        return "OK [copy]: " + path + " -> " + destPath;
    }

    std::string Move(const std::string &path, const std::string &destino)
    {
        std::string r = Copy(path, destino);
        if (r.rfind("Error", 0) == 0)
            return "Error [move]: " + r;
        std::string rr = Remove(path);
        if (rr.rfind("Error", 0) == 0)
            return "Error [move]: copia realizada pero no se pudo eliminar origen: " + rr;
        return "OK [move]: " + path + " -> " + destino;
    }

    static bool ResolveUser(const std::string &usersContent, const std::string &targetUser, int &uidOut, int &gidOut)
    {
        uidOut = -1;
        gidOut = -1;
        std::map<std::string, int> gidByGroup;
        std::istringstream ss(usersContent);
        std::string line;
        while (std::getline(ss, line))
        {
            if (line.empty())
                continue;
            std::vector<std::string> fields;
            std::istringstream ls(line);
            std::string token;
            while (std::getline(ls, token, ','))
                fields.push_back(token);
            if (fields.size() >= 3 && fields[1] == "G" && fields[0] != "0")
                gidByGroup[fields[2]] = std::stoi(fields[0]);
        }

        ss.clear();
        ss.seekg(0);
        while (std::getline(ss, line))
        {
            if (line.empty())
                continue;
            std::vector<std::string> fields;
            std::istringstream ls(line);
            std::string token;
            while (std::getline(ls, token, ','))
                fields.push_back(token);
            if (fields.size() >= 5 && fields[1] == "U" && fields[0] != "0" && fields[3] == targetUser)
            {
                uidOut = std::stoi(fields[0]);
                gidOut = gidByGroup.count(fields[2]) ? gidByGroup[fields[2]] : 1;
                return true;
            }
        }
        return false;
    }

    static void ApplyOwnerRecursive(std::fstream &f, const SuperBloque &sb, int inoIdx, int uid, int gid, bool recursive, std::set<int> &visited)
    {
        if (inoIdx < 0 || inoIdx >= sb.s_inodes_count || visited.count(inoIdx))
            return;
        visited.insert(inoIdx);

        Inodo ino{};
        if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
            return;
        ino.i_uid = uid;
        ino.i_gid = gid;
        Utilities::WriteObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo));

        if (!recursive || ino.i_type != '0')
            return;
        auto children = ListDirEntries(f, sb, inoIdx);
        for (const auto &child : children)
            ApplyOwnerRecursive(f, sb, child.second, uid, gid, recursive, visited);
    }

    std::string Chown(const std::string &path, const std::string &user, bool recursive)
    {
        if (!UserSession::IsLoggedIn())
            return "Error [chown]: no hay sesion activa";
        if (UserSession::GetCurrentUid() != 1)
            return "Error [chown]: solo root puede ejecutar chown";

        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [chown]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [chown]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        (void)partSize;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [chown]: no se pudo obtener limites";
        }
        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [chown]: particion no formateada";
        }

        Inodo usersIno{};
        if (!Utilities::ReadObject(f, usersIno, sb.s_inode_start))
        {
            f.close();
            return "Error [chown]: no se pudo leer users.txt";
        }
        std::string usersContent = ReadFileContent(f, sb, usersIno);
        int uid = -1, gid = -1;
        if (!ResolveUser(usersContent, user, uid, gid))
        {
            f.close();
            return "Error [chown]: usuario no encontrado: " + user;
        }

        std::string dummy, travErr;
        int target = (path == "/") ? 1 : TraversePathWithPerm(f, sb, path, false, dummy, UserSession::GetCurrentUid(), UserSession::GetCurrentGid(), travErr);
        if (target < 0)
        {
            f.close();
            return "Error [chown]: ruta no encontrada";
        }

        std::set<int> visited;
        ApplyOwnerRecursive(f, sb, target, uid, gid, recursive, visited);
        f.close();
        return "OK [chown]: propietario actualizado";
    }

    static void ApplyPermRecursive(std::fstream &f, const SuperBloque &sb, int inoIdx, const std::string &ugo, bool recursive, std::set<int> &visited)
    {
        if (inoIdx < 0 || inoIdx >= sb.s_inodes_count || visited.count(inoIdx))
            return;
        visited.insert(inoIdx);

        Inodo ino{};
        if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
            return;
        std::memcpy(ino.i_perm, ugo.c_str(), 3);
        Utilities::WriteObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo));

        if (!recursive || ino.i_type != '0')
            return;
        auto children = ListDirEntries(f, sb, inoIdx);
        for (const auto &child : children)
            ApplyPermRecursive(f, sb, child.second, ugo, recursive, visited);
    }

    std::string Chmod(const std::string &path, const std::string &ugo, bool recursive)
    {
        if (ugo.size() != 3 || !std::isdigit((unsigned char)ugo[0]) || !std::isdigit((unsigned char)ugo[1]) || !std::isdigit((unsigned char)ugo[2]))
            return "Error [chmod]: -ugo debe tener 3 digitos octales";
        if (ugo[0] > '7' || ugo[1] > '7' || ugo[2] > '7')
            return "Error [chmod]: permisos fuera de rango (0-7)";

        std::string err;
        const DiskManagement::MountedPartition *mp = nullptr;
        if (!GetActivePartition(mp, err))
            return "Error [chmod]: " + err;

        std::fstream f = Utilities::OpenFile(mp->diskPath);
        if (!f.is_open())
            return "Error [chmod]: no se pudo abrir el disco";

        int partStart = 0, partSize = 0;
        (void)partSize;
        if (!GetPartBounds(f, *mp, partStart, partSize))
        {
            f.close();
            return "Error [chmod]: no se pudo obtener limites";
        }
        SuperBloque sb{};
        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            f.close();
            return "Error [chmod]: particion no formateada";
        }

        std::string dummy, travErr;
        int target = (path == "/") ? 1 : TraversePathWithPerm(f, sb, path, false, dummy, UserSession::GetCurrentUid(), UserSession::GetCurrentGid(), travErr);
        if (target < 0)
        {
            f.close();
            return "Error [chmod]: ruta no encontrada";
        }

        Inodo targetIno{};
        if (!Utilities::ReadObject(f, targetIno, sb.s_inode_start + target * (int)sizeof(Inodo)))
        {
            f.close();
            return "Error [chmod]: no se pudo leer inodo";
        }
        if (UserSession::GetCurrentUid() != 1 && targetIno.i_uid != UserSession::GetCurrentUid())
        {
            f.close();
            return "Error [chmod]: solo root o propietario puede ejecutar chmod";
        }

        std::set<int> visited;
        ApplyPermRecursive(f, sb, target, ugo, recursive, visited);
        f.close();
        return "OK [chmod]: permisos actualizados";
    }

    static std::string EscapeJson(const std::string &s)
    {
        std::string out;
        out.reserve(s.size());
        for (char c : s)
        {
            if (c == '"')
                out += "\\\"";
            else if (c == '\\')
                out += "\\\\";
            else if (c == '\n')
                out += "\\n";
            else if (c == '\r')
                out += "\\r";
            else if (c == '\t')
                out += "\\t";
            else
                out += c;
        }
        return out;
    }

    static bool ResolveByMountedId(const std::string &id, std::fstream &f, SuperBloque &sb, int &partStart, std::string &err)
    {
        auto it = DiskManagement::MountMap.find(id);
        if (it == DiskManagement::MountMap.end())
        {
            err = "id no encontrado";
            return false;
        }

        f = Utilities::OpenFile(it->second.diskPath);
        if (!f.is_open())
        {
            err = "no se pudo abrir el disco";
            return false;
        }

        int partSize = 0;
        if (!GetPartBounds(f, it->second, partStart, partSize))
        {
            err = "no se pudo obtener limites de particion";
            f.close();
            return false;
        }

        if (!Utilities::ReadObject(f, sb, partStart) || sb.s_magic != 0xEF53)
        {
            err = "particion no formateada";
            f.close();
            return false;
        }
        return true;
    }

    static int TraversePathNoPerm(std::fstream &f, const SuperBloque &sb, const std::string &path)
    {
        if (path == "/")
            return 1;
        auto parts = SplitPath(path);
        if (parts.empty())
            return 1;

        int cur = 1;
        for (const auto &p : parts)
        {
            Inodo ino{};
            if (!Utilities::ReadObject(f, ino, sb.s_inode_start + cur * (int)sizeof(Inodo)) || ino.i_type != '0')
                return -1;
            int next = FindInDir(f, sb, cur, p);
            if (next == -1)
                return -1;
            cur = next;
        }
        return cur;
    }

    std::string BrowseJson(const std::string &id, const std::string &path)
    {
        std::fstream f;
        SuperBloque sb{};
        int partStart = 0;
        std::string err;
        if (!ResolveByMountedId(id, f, sb, partStart, err))
            return "{\"ok\":false,\"error\":\"" + EscapeJson(err) + "\"}";

        int inoIdx = TraversePathNoPerm(f, sb, path);
        if (inoIdx == -1)
        {
            f.close();
            return "{\"ok\":false,\"error\":\"ruta no encontrada\"}";
        }

        Inodo ino{};
        if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
        {
            f.close();
            return "{\"ok\":false,\"error\":\"no se pudo leer inodo\"}";
        }

        std::ostringstream out;
        out << "{\"ok\":true,\"id\":\"" << EscapeJson(id) << "\",\"path\":\"" << EscapeJson(path)
            << "\",\"inode\":" << inoIdx << ",\"type\":\"" << (ino.i_type == '0' ? "dir" : "file")
            << "\",\"items\":[";

        if (ino.i_type == '0')
        {
            auto entries = ListDirEntries(f, sb, inoIdx);
            for (size_t i = 0; i < entries.size(); i++)
            {
                Inodo cino{};
                Utilities::ReadObject(f, cino, sb.s_inode_start + entries[i].second * (int)sizeof(Inodo));
                std::string childPath = (path == "/") ? ("/" + entries[i].first) : (path + "/" + entries[i].first);
                if (i > 0)
                    out << ",";
                out << "{\"name\":\"" << EscapeJson(entries[i].first) << "\",\"path\":\"" << EscapeJson(childPath)
                    << "\",\"inode\":" << entries[i].second << ",\"kind\":\"" << (cino.i_type == '0' ? "dir" : "file")
                    << "\",\"size\":" << cino.i_size << "}";
            }
        }

        out << "]}";
        f.close();
        return out.str();
    }

    std::string ReadFileJson(const std::string &id, const std::string &path)
    {
        std::fstream f;
        SuperBloque sb{};
        int partStart = 0;
        std::string err;
        if (!ResolveByMountedId(id, f, sb, partStart, err))
            return "{\"ok\":false,\"error\":\"" + EscapeJson(err) + "\"}";

        int inoIdx = TraversePathNoPerm(f, sb, path);
        if (inoIdx == -1)
        {
            f.close();
            return "{\"ok\":false,\"error\":\"ruta no encontrada\"}";
        }

        Inodo ino{};
        if (!Utilities::ReadObject(f, ino, sb.s_inode_start + inoIdx * (int)sizeof(Inodo)))
        {
            f.close();
            return "{\"ok\":false,\"error\":\"no se pudo leer inodo\"}";
        }
        if (ino.i_type != '1')
        {
            f.close();
            return "{\"ok\":false,\"error\":\"la ruta no es archivo\"}";
        }

        std::string content = ReadFileContent(f, sb, ino);
        f.close();

        std::ostringstream out;
        out << "{\"ok\":true,\"id\":\"" << EscapeJson(id) << "\",\"path\":\"" << EscapeJson(path)
            << "\",\"inode\":" << inoIdx << ",\"size\":" << ino.i_size
            << ",\"content\":\"" << EscapeJson(content) << "\"}";
        return out.str();
    }

} // namespace FileOperations
