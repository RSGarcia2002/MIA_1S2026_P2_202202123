// FileSystem: formateo EXT2/EXT3 + journaling + loss
#include "FileSystem.h"
#include "../Analyzer/Analyzer.h"
#include "../DiskManagement/DiskManagement.h"
#include "../UserSession/UserSession.h"
#include "../Utilities/Utilities.h"
#include "../Structs/Structs.h"

#include <cstring>
#include <ctime>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <vector>

namespace FileSystem
{
    static const int JOURNAL_MAX_ENTRIES = 50;

    static std::string ToLowerCopy(const std::string &s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c)
                       { return (char)std::tolower(c); });
        return out;
    }

    static std::string TrimCopy(const std::string &s)
    {
        size_t a = 0;
        while (a < s.size() && std::isspace((unsigned char)s[a]))
            a++;
        size_t b = s.size();
        while (b > a && std::isspace((unsigned char)s[b - 1]))
            b--;
        return s.substr(a, b - a);
    }

    static std::string Truncate(const std::string &s, size_t n)
    {
        if (s.size() <= n)
            return s;
        return s.substr(0, n);
    }

    static std::string ExtractParamValue(const std::string &line, const std::string &key)
    {
        std::string low = ToLowerCopy(line);
        std::string target = "-" + ToLowerCopy(key) + "=";
        auto pos = low.find(target);
        if (pos == std::string::npos)
            return "";

        size_t start = pos + target.size();
        if (start >= line.size())
            return "";

        if (line[start] == '"')
        {
            size_t end = line.find('"', start + 1);
            if (end == std::string::npos)
                return line.substr(start + 1);
            return line.substr(start + 1, end - start - 1);
        }

        size_t end = start;
        while (end < line.size() && !std::isspace((unsigned char)line[end]))
            end++;
        return line.substr(start, end - start);
    }

    static bool ResolvePartitionById(const std::string &id,
                                     DiskManagement::MountedPartition &mpOut,
                                     std::fstream &fileOut,
                                     int &partStart,
                                     int &partSize,
                                     std::string &err)
    {
        auto it = DiskManagement::MountMap.find(id);
        if (it == DiskManagement::MountMap.end())
        {
            err = "id no encontrado en particiones montadas: " + id;
            return false;
        }

        mpOut = it->second;
        fileOut = Utilities::OpenFile(mpOut.diskPath);
        if (!fileOut.is_open())
        {
            err = "no se pudo abrir el disco: " + mpOut.diskPath;
            return false;
        }

        if (mpOut.isLogical)
        {
            EBR ebr{};
            if (!Utilities::ReadObject(fileOut, ebr, mpOut.ebrPos))
            {
                err = "no se pudo leer el EBR de la particion logica";
                fileOut.close();
                return false;
            }
            partStart = ebr.Start;
            partSize = ebr.Size;
        }
        else
        {
            MBR mbr{};
            if (!Utilities::ReadObject(fileOut, mbr, 0))
            {
                err = "no se pudo leer el MBR";
                fileOut.close();
                return false;
            }
            if (mpOut.partIndex < 0 || mpOut.partIndex >= 4)
            {
                err = "indice de particion invalido";
                fileOut.close();
                return false;
            }
            partStart = mbr.Partitions[mpOut.partIndex].Start;
            partSize = mbr.Partitions[mpOut.partIndex].Size;
        }

        return true;
    }

    static int CalcN(int partSize, int journalEntries)
    {
        int header = (int)sizeof(SuperBloque) + journalEntries * (int)sizeof(JournalEntry);
        int denom = 4 + (int)sizeof(Inodo) + 3 * (int)sizeof(BloqueFile);
        int avail = partSize - header;
        if (avail <= 0 || denom <= 0)
            return 0;
        return avail / denom;
    }

    static bool FillBytes(std::fstream &f, int pos, int count, char val = '\0')
    {
        f.seekp(pos, std::ios::beg);
        if (!f)
            return false;

        char buf[1024];
        std::memset(buf, val, sizeof(buf));
        int rem = count;
        while (rem > 0)
        {
            int chunk = std::min(rem, (int)sizeof(buf));
            f.write(buf, chunk);
            if (!f)
                return false;
            rem -= chunk;
        }
        return true;
    }

    static int JournalStart(int partStart)
    {
        return partStart + (int)sizeof(SuperBloque);
    }

    static int JournalCapacity(const SuperBloque &sb, int partStart)
    {
        if (sb.s_filesystem_type != 3)
            return 0;

        int bytes = sb.s_bm_inode_start - JournalStart(partStart);
        if (bytes <= 0)
            return 0;
        return bytes / (int)sizeof(JournalEntry);
    }

    static void AppendJournalEntry(std::fstream &file, const SuperBloque &sb, int partStart,
                                   const std::string &command, const std::string &detail)
    {
        int cap = JournalCapacity(sb, partStart);
        if (cap <= 0)
            return;

        int jStart = JournalStart(partStart);
        int target = -1;

        for (int i = 0; i < cap; i++)
        {
            JournalEntry je{};
            if (!Utilities::ReadObject(file, je, jStart + i * (int)sizeof(JournalEntry)))
                return;
            if (je.active == 0)
            {
                target = i;
                break;
            }
        }

        if (target == -1)
        {
            for (int i = 1; i < cap; i++)
            {
                JournalEntry je{};
                if (!Utilities::ReadObject(file, je, jStart + i * (int)sizeof(JournalEntry)))
                    return;
                if (!Utilities::WriteObject(file, je, jStart + (i - 1) * (int)sizeof(JournalEntry)))
                    return;
            }
            target = cap - 1;
        }

        JournalEntry je{};
        je.active = 1;
        je.timestamp = (int64_t)std::time(nullptr);
        std::string cmd = Truncate(command, sizeof(je.command) - 1);
        std::string det = Truncate(detail, sizeof(je.detail) - 1);
        std::strncpy(je.command, cmd.c_str(), sizeof(je.command) - 1);
        std::strncpy(je.detail, det.c_str(), sizeof(je.detail) - 1);
        Utilities::WriteObject(file, je, jStart + target * (int)sizeof(JournalEntry));
    }

    std::string Mkfs(const std::string &id, const std::string &type, const std::string &fs)
    {
        (void)type; // se conserva por compatibilidad; en este proyecto se aplica full.

        std::fstream file;
        DiskManagement::MountedPartition mp{};
        int partStart = 0, partSize = 0;
        std::string err;

        if (!ResolvePartitionById(id, mp, file, partStart, partSize, err))
            return "Error [mkfs]: " + err;

        std::string fsLower = ToLowerCopy(fs);
        int fsType = (fsLower == "3fs") ? 3 : 2;
        int journalEntries = (fsType == 3) ? JOURNAL_MAX_ENTRIES : 0;

        int n = CalcN(partSize, journalEntries);
        if (n < 2)
        {
            file.close();
            return "Error [mkfs]: particion demasiado pequenia (n=" + std::to_string(n) + ")";
        }

        int bmInodeStart = partStart + (int)sizeof(SuperBloque) + journalEntries * (int)sizeof(JournalEntry);
        int bmBlockStart = bmInodeStart + n;
        int inodeStart = bmBlockStart + 3 * n;
        int blockStart = inodeStart + n * (int)sizeof(Inodo);

        SuperBloque sb{};
        sb.s_filesystem_type = fsType;
        sb.s_inodes_count = n;
        sb.s_blocks_count = 3 * n;
        sb.s_free_inodes_count = n - 2;
        sb.s_free_blocks_count = 3 * n - 2;
        sb.s_mtime = (int64_t)std::time(nullptr);
        sb.s_umtime = 0;
        sb.s_mnt_count = 1;
        sb.s_magic = 0xEF53;
        sb.s_inode_s = (int32_t)sizeof(Inodo);
        sb.s_block_s = (int32_t)sizeof(BloqueFile);
        sb.s_firts_ino = 2;
        sb.s_first_blo = 2;
        sb.s_bm_inode_start = bmInodeStart;
        sb.s_bm_block_start = bmBlockStart;
        sb.s_inode_start = inodeStart;
        sb.s_block_start = blockStart;

        // Limpiar completamente el area de la particion
        FillBytes(file, partStart, partSize, '\0');

        // Inicializar area de journal para EXT3
        if (journalEntries > 0)
            FillBytes(file, partStart + (int)sizeof(SuperBloque), journalEntries * (int)sizeof(JournalEntry), '\0');

        FillBytes(file, bmInodeStart, n, '\0');
        FillBytes(file, bmBlockStart, 3 * n, '\0');

        char one = 1;
        file.seekp(bmInodeStart, std::ios::beg);
        file.write(&one, 1);
        file.seekp(bmInodeStart + 1, std::ios::beg);
        file.write(&one, 1);
        file.seekp(bmBlockStart, std::ios::beg);
        file.write(&one, 1);
        file.seekp(bmBlockStart + 1, std::ios::beg);
        file.write(&one, 1);

        const std::string usersContent = "1,G,root\n1,U,root,root,123\n";

        int64_t now = (int64_t)std::time(nullptr);
        Inodo inodo0{};
        inodo0.i_uid = 1;
        inodo0.i_gid = 1;
        inodo0.i_size = (int32_t)usersContent.size();
        inodo0.i_atime = now;
        inodo0.i_ctime = now;
        inodo0.i_mtime = now;
        for (int k = 0; k < 15; k++)
            inodo0.i_block[k] = -1;
        inodo0.i_block[0] = 0;
        inodo0.i_type = '1';
        std::memcpy(inodo0.i_perm, "664", 3);

        Inodo inodo1{};
        inodo1.i_uid = 1;
        inodo1.i_gid = 1;
        inodo1.i_size = (int32_t)sizeof(BloqueDir);
        inodo1.i_atime = now;
        inodo1.i_ctime = now;
        inodo1.i_mtime = now;
        for (int k = 0; k < 15; k++)
            inodo1.i_block[k] = -1;
        inodo1.i_block[0] = 1;
        inodo1.i_type = '0';
        std::memcpy(inodo1.i_perm, "755", 3);

        Utilities::WriteObject(file, inodo0, inodeStart);
        Utilities::WriteObject(file, inodo1, inodeStart + (int)sizeof(Inodo));

        BloqueFile bfile{};
        std::memset(bfile.b_content, '\0', sizeof(bfile.b_content));
        std::memcpy(bfile.b_content, usersContent.c_str(), std::min((int)usersContent.size(), (int)sizeof(bfile.b_content)));
        Utilities::WriteObject(file, bfile, blockStart);

        BloqueDir bdir{};
        for (int k = 0; k < 4; k++)
        {
            bdir.b_content[k].b_name[0] = '\0';
            bdir.b_content[k].b_inodo = -1;
        }
        std::strncpy(bdir.b_content[0].b_name, ".", 12);
        bdir.b_content[0].b_inodo = 1;
        std::strncpy(bdir.b_content[1].b_name, "..", 12);
        bdir.b_content[1].b_inodo = 1;
        std::strncpy(bdir.b_content[2].b_name, "users.txt", 12);
        bdir.b_content[2].b_inodo = 0;
        Utilities::WriteObject(file, bdir, blockStart + (int)sizeof(BloqueFile));

        Utilities::WriteObject(file, sb, partStart);

        if (fsType == 3)
            AppendJournalEntry(file, sb, partStart, "mkfs -id=" + id + " -fs=3fs", "formateo EXT3");

        file.close();

        std::ostringstream out;
        out << "MKFS exitoso\n"
            << "   Tipo FS:    EXT" << fsType << "\n"
            << "   n (inodos): " << n << "\n"
            << "   Bloques:    " << 3 * n << "\n"
            << "   Journal:    " << journalEntries << " entradas\n"
            << "   users.txt creado con root:root:123";
        return out.str();
    }

    std::string Loss(const std::string &id)
    {
        std::fstream file;
        DiskManagement::MountedPartition mp{};
        int partStart = 0, partSize = 0;
        std::string err;

        if (!ResolvePartitionById(id, mp, file, partStart, partSize, err))
            return "Error [loss]: " + err;

        SuperBloque sb{};
        if (!Utilities::ReadObject(file, sb, partStart) || sb.s_magic != 0xEF53)
        {
            file.close();
            return "Error [loss]: particion no formateada";
        }
        if (sb.s_filesystem_type != 3)
        {
            file.close();
            return "Error [loss]: solo aplica a EXT3";
        }

        AppendJournalEntry(file, sb, partStart, "loss -id=" + id, "simulacion de perdida");

        int dataStart = sb.s_bm_inode_start;
        int dataSize = (partStart + partSize) - dataStart;
        if (dataSize > 0)
            FillBytes(file, dataStart, dataSize, '\0');

        sb.s_free_inodes_count = sb.s_inodes_count;
        sb.s_free_blocks_count = sb.s_blocks_count;
        sb.s_firts_ino = 0;
        sb.s_first_blo = 0;
        sb.s_umtime = (int64_t)std::time(nullptr);
        Utilities::WriteObject(file, sb, partStart);

        file.close();
        return "LOSS ejecutado: estructura EXT3 marcada como perdida (bitmaps/inodos/bloques limpiados)";
    }

    std::string Journaling(const std::string &id)
    {
        std::fstream file;
        DiskManagement::MountedPartition mp{};
        int partStart = 0, partSize = 0;
        std::string err;

        if (!ResolvePartitionById(id, mp, file, partStart, partSize, err))
            return "Error [journaling]: " + err;

        SuperBloque sb{};
        if (!Utilities::ReadObject(file, sb, partStart) || sb.s_magic != 0xEF53)
        {
            file.close();
            return "Error [journaling]: particion no formateada";
        }
        if (sb.s_filesystem_type != 3)
        {
            file.close();
            return "Error [journaling]: la particion no es EXT3";
        }

        int cap = JournalCapacity(sb, partStart);
        int jStart = JournalStart(partStart);
        std::ostringstream out;
        out << "JOURNALING EXT3 (id=" << id << ")\n";

        int count = 0;
        for (int i = 0; i < cap; i++)
        {
            JournalEntry je{};
            if (!Utilities::ReadObject(file, je, jStart + i * (int)sizeof(JournalEntry)))
                break;
            if (je.active == 0)
                continue;

            out << i << ". " << je.timestamp << " | " << je.command;
            if (std::strlen(je.detail) > 0)
                out << " | " << je.detail;
            out << "\n";
            count++;
        }

        file.close();
        if (count == 0)
            out << "(sin entradas)\n";
        return out.str();
    }

    std::string Recovery(const std::string &id)
    {
        std::fstream file;
        DiskManagement::MountedPartition mp{};
        int partStart = 0, partSize = 0;
        std::string err;

        if (!ResolvePartitionById(id, mp, file, partStart, partSize, err))
            return "Error [recovery]: " + err;

        SuperBloque sb{};
        if (!Utilities::ReadObject(file, sb, partStart) || sb.s_magic != 0xEF53)
        {
            file.close();
            return "Error [recovery]: particion no formateada";
        }
        if (sb.s_filesystem_type != 3)
        {
            file.close();
            return "Error [recovery]: solo aplica a EXT3";
        }

        int cap = JournalCapacity(sb, partStart);
        int jStart = JournalStart(partStart);
        std::vector<std::string> replay;

        for (int i = 0; i < cap; i++)
        {
            JournalEntry je{};
            if (!Utilities::ReadObject(file, je, jStart + i * (int)sizeof(JournalEntry)))
                break;
            if (je.active == 0 || std::strlen(je.command) == 0)
                continue;
            replay.push_back(std::string(je.command));
        }

        file.close();

        if (replay.empty())
            return "Error [recovery]: journal vacio, no hay operaciones para recuperar";

        std::string mk = Mkfs(id, "full", "3fs");
        if (mk.rfind("Error", 0) == 0)
            return "Error [recovery]: no se pudo re-formatear para recuperar: " + mk;

        UserSession::Logout();

        int executed = 0;
        int failed = 0;
        std::ostringstream failOut;

        for (const auto &raw : replay)
        {
            std::string cmd = TrimCopy(raw);
            if (cmd.empty())
                continue;

            std::istringstream iss(cmd);
            std::string op;
            iss >> op;
            op = ToLowerCopy(op);
            if (op == "mkfs" || op == "loss" || op == "journaling" || op == "recovery")
                continue;

            std::string res = Analyzer::Analyze(cmd);
            if (res.rfind("Error", 0) == 0)
            {
                failed++;
                failOut << " - " << cmd << " => " << res << "\n";
            }
            else
            {
                executed++;
            }
        }

        std::ostringstream out;
        out << "RECOVERY completado\n"
            << "   ID: " << id << "\n"
            << "   Reejecutados: " << executed << "\n"
            << "   Fallidos: " << failed;
        if (failed > 0)
            out << "\nDetalle de fallos:\n"
                << failOut.str();
        return out.str();
    }

    void AutoJournalFromCommand(const std::string &line, const std::string &result)
    {
        if (line.empty())
            return;
        if (result.rfind("Error", 0) == 0)
            return;

        std::string trimmed = TrimCopy(line);
        if (trimmed.empty() || trimmed[0] == '#')
            return;

        std::istringstream iss(trimmed);
        std::string command;
        iss >> command;
        command = ToLowerCopy(command);

        if (command == "cat" || command == "rep" || command == "mounted" || command == "journaling" || command == "find")
            return;

        std::string id = ExtractParamValue(trimmed, "id");
        if (id.empty() && UserSession::IsLoggedIn())
            id = UserSession::GetCurrentId();
        if (id.empty())
            return;

        std::fstream file;
        DiskManagement::MountedPartition mp{};
        int partStart = 0, partSize = 0;
        std::string err;
        if (!ResolvePartitionById(id, mp, file, partStart, partSize, err))
            return;

        SuperBloque sb{};
        if (!Utilities::ReadObject(file, sb, partStart) || sb.s_magic != 0xEF53 || sb.s_filesystem_type != 3)
        {
            file.close();
            return;
        }

        AppendJournalEntry(file, sb, partStart, Truncate(trimmed, 90), Truncate(result, 90));
        file.close();
    }

} // namespace FileSystem
