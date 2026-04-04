#include "registerarchives.hpp"

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <boost/filesystem.hpp>

#include <components/debug/debuglog.hpp>

#include <components/vfs/manager.hpp>
#include <components/vfs/bsaarchive.hpp>
#include <components/vfs/filesystemarchive.hpp>

namespace VFS
{

    void registerArchives(VFS::Manager *vfs, const Files::Collections &collections,
                          const std::vector<std::string> &archives, bool useLooseFiles)
    {
        const Files::PathContainer& dataDirs = collections.getPaths();

        // EncoreMP: собираем все .bsa из data-директорий автоматически.
        // Сначала грузим приоритетные архивы ванильной игры (если существуют),
        // затем остальные в алфавитном порядке.
        // Это заменяет жёсткий список fallback-archive из openmw.cfg —
        // Morrowind.bsa не обязателен для запуска.

        const std::vector<std::string> priorityBSAs = {
            "Morrowind.bsa",
            "Tribunal.bsa",
            "Bloodmoon.bsa"
        };

        // Собираем все .bsa файлы из всех data-директорий
        std::set<boost::filesystem::path> seenDirs;
        std::vector<std::string> allFound;   // полные пути к найденным bsa
        std::vector<std::string> foundNames; // только имена (для дедупликации)

        auto addBsaIfNew = [&](const std::string& fullPath, const std::string& name) {
            for (const auto& n : foundNames)
                if (boost::filesystem::path(n).filename().string() ==
                    boost::filesystem::path(name).filename().string())
                    return; // уже есть
            foundNames.push_back(name);
            allFound.push_back(fullPath);
        };

        // Шаг 1: приоритетные BSA
        for (const std::string& bsaName : priorityBSAs)
        {
            if (collections.doesExist(bsaName))
            {
                const std::string path = collections.getPath(bsaName).string();
                addBsaIfNew(path, bsaName);
            }
        }

        // Шаг 2: сканируем data-директории, собираем остальные .bsa по алфавиту
        for (const auto& dir : dataDirs)
        {
            if (!seenDirs.insert(dir).second) continue;
            if (!boost::filesystem::is_directory(dir)) continue;

            // Собрать все .bsa в этой директории
            std::vector<std::string> dirBsas;
            for (const auto& entry : boost::filesystem::directory_iterator(dir))
            {
                if (!boost::filesystem::is_regular_file(entry)) continue;
                std::string ext = entry.path().extension().string();
                // case-insensitive .bsa check
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".bsa") continue;
                dirBsas.push_back(entry.path().string());
            }

            // Сортируем по имени файла (алфавитный порядок)
            std::sort(dirBsas.begin(), dirBsas.end(), [](const std::string& a, const std::string& b) {
                return boost::filesystem::path(a).filename().string() <
                       boost::filesystem::path(b).filename().string();
            });

            for (const std::string& bsaPath : dirBsas)
            {
                const std::string name = boost::filesystem::path(bsaPath).filename().string();
                addBsaIfNew(bsaPath, name);
            }
        }

        // Шаг 3: добавляем найденные архивы (последний имеет наибольший приоритет)
        for (const std::string& archivePath : allFound)
        {
            Log(Debug::Info) << "Adding BSA archive " << archivePath;
            vfs->addArchive(new BsaArchive(archivePath));
        }

        // Шаг 4: явно указанные архивы в openmw.cfg (если есть и не уже добавлены)
        for (const std::string& archive : archives)
        {
            bool alreadyAdded = false;
            for (const std::string& n : foundNames)
                if (boost::filesystem::path(n).filename().string() == archive) {
                    alreadyAdded = true; break;
                }
            if (alreadyAdded) continue;

            if (collections.doesExist(archive))
            {
                const std::string archivePath = collections.getPath(archive).string();
                Log(Debug::Info) << "Adding BSA archive (explicit) " << archivePath;
                vfs->addArchive(new BsaArchive(archivePath));
            }
            else
            {
                // EncoreMP: не бросаем исключение если BSA отсутствует —
                // просто предупреждаем. Morrowind.bsa необязателен.
                Log(Debug::Warning) << "Archive '" << archive << "' not found, skipping";
            }
        }

        // Шаг 5: добавляем loose-файлы (data-директории)
        if (useLooseFiles)
        {
            std::set<boost::filesystem::path> seen2;
            for (const auto& iter : dataDirs)
            {
                if (seen2.insert(iter).second)
                {
                    Log(Debug::Info) << "Adding data directory " << iter.string();
                    vfs->addArchive(new FileSystemArchive(iter.string()));
                }
                else
                    Log(Debug::Info) << "Ignoring duplicate data directory " << iter.string();
            }
        }

        vfs->buildIndex();
    }

} // namespace VFS
