#include "DbpfIndexService.hpp"

#include <spdlog/spdlog.h>

#include <ranges>
#include <utility>

#include "DBPFReader.h"
#include "ParseTypes.h"

DbpfIndexService::DbpfIndexService(PluginLocator  locator) : locator_(std::move(locator)) {}

DbpfIndexService::~DbpfIndexService() { shutdown(); }

void DbpfIndexService::start() {
    if (running_) {
        return;
    }

    stop_ = false;
    done_ = false;
    totalFiles_ = 0;
    processedFiles_ = 0;
    entriesIndexed_ = 0;
    errorCount_ = 0;

    {
        std::unique_lock lock(mutex_);
        currentFile_.clear();
        files_.clear();
        tgiToFiles_.clear();
        typeInstanceToTgis_.clear();
    }

    running_ = true;
    workerThread_ = std::thread([this] { worker_(); });
}

void DbpfIndexService::shutdown() {
    stop_ = true;
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    running_ = false;
}

auto DbpfIndexService::isRunning() const -> bool {
    return running_;
}

auto DbpfIndexService::snapshot() const -> ScanProgress {
    std::shared_lock lock(mutex_);
    return ScanProgress{
        .totalFiles = totalFiles_,
        .processedFiles = processedFiles_,
        .entriesIndexed = entriesIndexed_,
        .errorCount = errorCount_,
        .currentFile = currentFile_,
        .done = done_
    };
}

auto DbpfIndexService::tgiIndex() const -> const std::unordered_map<DBPF::Tgi, std::vector<std::filesystem::path>, DBPF::TgiHash>& {
    std::shared_lock lock(mutex_);
    return tgiToFiles_;
}

auto DbpfIndexService::typeInstanceIndex() const -> const std::unordered_map<uint64_t, std::vector<DBPF::Tgi>>& {
    std::shared_lock lock(mutex_);
    return typeInstanceToTgis_;
}

auto DbpfIndexService::typeIndex() const -> const std::unordered_map<uint32_t, std::vector<DBPF::Tgi>>& {
    std::shared_lock lock(mutex_);
    return typeToTgis_;
}

auto DbpfIndexService::typeIndex(const uint32_t type) -> std::vector<DBPF::Tgi> {
    std::shared_lock lock(mutex_);
    if (typeToTgis_.contains(type)) {
        return typeToTgis_.at(type);
    }
    return {};
}

auto DbpfIndexService::dbpfFiles() const -> const std::vector<std::filesystem::path>& {
    return files_;
}

auto DbpfIndexService::pluginLocator() const -> const PluginLocator& {
    return locator_;
}

ParseExpected<const Exemplar::Record*> DbpfIndexService::loadExemplar(const DBPF::Tgi& tgi) const {
    // Check cache first (with read lock)
    {
        std::shared_lock readLock(mutex_);
        auto cacheIt = exemplarCache_.find(tgi);
        if (cacheIt != exemplarCache_.end()) {
            return &cacheIt->second;
        }
    }

    // Not in cache - need to load it
    // Find which file(s) contain this TGI
    std::shared_lock readLock(mutex_);
    auto tgiIt = tgiToFiles_.find(tgi);
    if (tgiIt == tgiToFiles_.end() || tgiIt->second.empty()) {
        return Fail("TGI not found in index");
    }

    const auto& filePaths = tgiIt->second;
    readLock.unlock();

    // Try to load from the last file that has it
    for (const auto& filePath : std::ranges::reverse_view(filePaths)) {
        auto* reader = getReader(filePath);
        if (!reader) {
            continue;
        }

        auto exemplar = reader->LoadExemplar(tgi);
        if (exemplar.has_value()) {
            // Insert into cache and return pointer to cached version
            std::unique_lock writeLock(mutex_);
            auto [it, inserted] = exemplarCache_.try_emplace(tgi, std::move(*exemplar));
            return &it->second;
        }
    }

    return Fail("Failed to load exemplar from any file");
}

std::optional<std::vector<uint8_t>> DbpfIndexService::loadEntryData(const DBPF::Tgi& tgi) const {
    // Find which file(s) contain this TGI
    std::shared_lock readLock(mutex_);
    auto tgiIt = tgiToFiles_.find(tgi);
    if (tgiIt == tgiToFiles_.end() || tgiIt->second.empty()) {
        return std::nullopt;
    }

    const auto& filePaths = tgiIt->second;
    readLock.unlock();

    // Try to load from the last file that has it
    for (const auto & filePath : std::ranges::reverse_view(filePaths)) {
        const auto* reader = getReader(filePath);
        if (!reader) {
            continue;
        }

        auto data = reader->ReadEntryData(tgi);
        if (data.has_value()) {
            return data;
        }
    }

    return std::nullopt;
}

DBPF::Reader* DbpfIndexService::getReader(const std::filesystem::path& filePath) const {
    std::unique_lock lock(mutex_);

    // Check if we already have a reader for this file
    auto it = readerCache_.find(filePath);
    if (it != readerCache_.end()) {
        return it->second.get();
    }

    // Create a new reader and load the file
    auto reader = std::make_unique<DBPF::Reader>();
    if (!reader->LoadFile(filePath.string())) {
        return nullptr;
    }

    // Cache it and return
    auto* readerPtr = reader.get();
    readerCache_[filePath] = std::move(reader);
    return readerPtr;
}

void DbpfIndexService::worker_() {
    try {
        const auto pluginFiles = locator_.ListDbpfFiles();

        {
            std::unique_lock lock(mutex_);
            files_ = pluginFiles;
            totalFiles_ = pluginFiles.size();
        }

        for (const auto& filePath : pluginFiles) {
            if (stop_) {
                break;
            }

            {
                std::unique_lock lock(mutex_);
                currentFile_ = filePath.filename().string();
            }

            try {
                DBPF::Reader reader;
                if (!reader.LoadFile(filePath.string())) {
                    spdlog::warn("Failed to load {}, not a DBPF file?", filePath.string());
                    ++errorCount_;
                    ++processedFiles_;
                    continue;
                }

                const auto& index = reader.GetIndex();
                size_t entriesCount = 0;

                for (const auto& entry : index) {
                    if (stop_) break;

                    // Create a type-instance key for indexing
                    uint64_t typeInstanceKey = (static_cast<uint64_t>(entry.tgi.type) << 32) | entry.tgi.instance;

                    {
                        std::unique_lock lock(mutex_);
                        typeInstanceToTgis_[typeInstanceKey].push_back(entry.tgi);
                        typeToTgis_[entry.tgi.type].push_back(entry.tgi);
                        tgiToFiles_[entry.tgi].push_back(filePath);
                    }

                    entriesCount++;
                }

                {
                    std::unique_lock lock(mutex_);
                    entriesIndexed_ += entriesCount;
                    ++processedFiles_;
                }

            } catch ([[maybe_unused]] const std::exception& error) {
                spdlog::error("Error loading {}", currentFile_);
                ++errorCount_;
                ++processedFiles_;
            }
        }

        {
            std::unique_lock lock(mutex_);
            done_ = true;
            currentFile_.clear();
        }

    } catch (const std::exception& error) {
        ++errorCount_;
        done_ = true;
    }
}

void DbpfIndexService::publishProgress_() {
    // Could be used to notify observers of progress
    // For now, kept simple - snapshots can be taken with snapshot()
}

