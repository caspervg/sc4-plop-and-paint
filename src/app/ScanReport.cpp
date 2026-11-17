#include "ScanReport.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <fstream>
#include <optional>
#include <string>

#include "PathText.hpp"

namespace fs = std::filesystem;

namespace {
    constexpr char kSeparator = static_cast<char>(fs::path::preferred_separator);

    struct ReportEntry {
        std::vector<std::string> components; // relative to the root, UTF-8
        std::vector<std::string> sortKeys;   // the same, ASCII letters uppercased
        std::string status;
    };

    auto UpperAscii(std::string text) -> std::string {
        std::ranges::transform(text, text.begin(), [](const char c) {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        });
        return text;
    }

    auto MakeEntry(const fs::path& root, const fs::path& file, std::string status) -> ReportEntry {
        ReportEntry entry;
        for (const auto& component : file.lexically_relative(root)) {
            entry.components.push_back(PathToUtf8(component));
            entry.sortKeys.push_back(UpperAscii(entry.components.back()));
        }
        if (entry.components.empty()) {
            entry.components.push_back(PathToUtf8(file));
            entry.sortKeys.push_back(UpperAscii(entry.components.back()));
        }
        entry.status = std::move(status);
        return entry;
    }

    // Folder order like the game walks it: the files of a folder first, then its subfolders.
    auto TreeLess(const ReportEntry& a, const ReportEntry& b) -> bool {
        const size_t depth = std::min(a.sortKeys.size(), b.sortKeys.size());
        for (size_t i = 0; i < depth; ++i) {
            const bool aIsFile = i + 1 == a.sortKeys.size();
            const bool bIsFile = i + 1 == b.sortKeys.size();
            if (aIsFile != bIsFile) {
                return aIsFile;
            }
            if (a.sortKeys[i] != b.sortKeys[i]) {
                return a.sortKeys[i] < b.sortKeys[i];
            }
        }
        return a.sortKeys.size() < b.sortKeys.size();
    }

    auto Plural(const size_t count, std::string_view singular, std::string_view plural) -> std::string {
        return std::format("{} {}", count, count == 1 ? singular : plural);
    }

    auto DescribeIndexed(const size_t loadOrder, const IndexedFileResult& result,
                         const std::optional<FileItemCounts>& counts) -> std::string {
        switch (result.state) {
            case IndexedFileResult::State::NotRead:
                return std::format("load #{}, NOT READ: indexing stopped before this file", loadOrder);
            case IndexedFileResult::State::Failed:
                return std::format("load #{}, NOT READ: not a valid DBPF file, or it could not be opened", loadOrder);
            case IndexedFileResult::State::Indexed:
                break;
        }

        auto status = std::format("load #{}, {}", loadOrder, Plural(result.entryCount, "entry", "entries"));
        if (counts) {
            std::vector<std::string> parts;
            if (counts->buildings) parts.push_back(Plural(counts->buildings, "building", "buildings"));
            if (counts->lots) parts.push_back(Plural(counts->lots, "lot", "lots"));
            if (counts->props) parts.push_back(Plural(counts->props, "prop", "props"));
            if (counts->flora) parts.push_back(Plural(counts->flora, "flora item", "flora items"));
            if (!parts.empty()) {
                status += ", in cache: ";
                for (size_t i = 0; i < parts.size(); ++i) {
                    status += (i ? ", " : "") + parts[i];
                }
            }
            if (counts->unreadableRecords) {
                status += ", " + Plural(counts->unreadableRecords, "record", "records") + " COULD NOT BE READ";
            }
        }
        return status;
    }

    void WriteTree(std::ofstream& out, std::vector<ReportEntry>& entries) {
        std::ranges::sort(entries, TreeLess);
        std::vector<std::string> openFolders;
        for (const auto& entry : entries) {
            const size_t folderDepth = entry.components.size() - 1;
            size_t common = 0;
            while (common < openFolders.size() && common < folderDepth &&
                   openFolders[common] == entry.components[common]) {
                ++common;
            }
            openFolders.resize(common);
            for (size_t i = common; i < folderDepth; ++i) {
                out << std::string(2 * (i + 1), ' ') << entry.components[i] << kSeparator << '\n';
                openFolders.push_back(entry.components[i]);
            }
            out << std::string(2 * (folderDepth + 1), ' ') << entry.components.back() << "  [" << entry.status
                << "]\n";
        }
    }
}

bool WriteScanReport(const fs::path& outputPath,
                     const std::string_view version,
                     const std::vector<fs::path>& files,
                     const std::vector<IndexedFileResult>& results,
                     const PluginScanReport& report,
                     const std::unordered_map<fs::path, FileItemCounts>& itemCounts) {
    std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }

    size_t indexed = 0;
    size_t failed = 0;
    size_t notRead = 0;
    for (const auto& result : results) {
        switch (result.state) {
            case IndexedFileResult::State::Indexed: ++indexed; break;
            case IndexedFileResult::State::Failed: ++failed; break;
            case IndexedFileResult::State::NotRead: ++notRead; break;
        }
    }

    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    out << "SC4 Plop and Paint - scanned plugin files\n";
    out << std::format("Cache builder {}, {:%Y-%m-%d %H:%M:%S} UTC\n\n", version, now);
    out << "Every file in the plugin folders, by folder. \"load #\" is the file's place in the game's load\n"
           "order: a later file overrides earlier files that hold the same items. \"in cache\" counts the items\n"
           "whose winning copy comes from that file; buildings only show up in the game through a lot.\n\n";
    out << std::format("DBPF files: {} read, {} could not be read, {} not reached\n", indexed, failed, notRead);
    out << std::format("Other files skipped: {}\n", report.skippedFiles.size());
    out << std::format("Folders that could not be read: {}\n", report.unreadableFolders.size());

    for (size_t rootIndex = 0; rootIndex < report.roots.size(); ++rootIndex) {
        const auto& root = report.roots[rootIndex];
        std::vector<ReportEntry> entries;
        for (size_t i = root.firstFile; i < root.firstFile + root.fileCount && i < files.size(); ++i) {
            const auto counts = itemCounts.find(files[i]);
            const IndexedFileResult result = i < results.size() ? results[i] : IndexedFileResult{};
            entries.push_back(MakeEntry(root.path, files[i],
                                        DescribeIndexed(i + 1, result,
                                                        counts != itemCounts.end()
                                                            ? std::optional(counts->second)
                                                            : std::nullopt)));
        }
        for (const auto& skipped : report.skippedFiles) {
            if (skipped.root == rootIndex) {
                entries.push_back(MakeEntry(root.path, skipped.path, "skipped: not a DBPF file"));
            }
        }

        out << std::format("\n{}{}: {}\n", root.label, root.recursive ? "" : " (top level only)",
                           root.path.empty() ? "(not set)" : PathToUtf8(root.path));
        if (entries.empty()) {
            out << "  (no files)\n";
            continue;
        }
        WriteTree(out, entries);
    }

    if (!report.unreadableFolders.empty()) {
        out << "\nFolders that could not be read:\n";
        for (const auto& folder : report.unreadableFolders) {
            out << "  " << PathToUtf8(folder.path) << "  [" << folder.error << "]\n";
        }
    }

    out.flush();
    return out.good();
}
