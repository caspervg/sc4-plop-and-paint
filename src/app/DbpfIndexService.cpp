#include "DbpfIndexService.hpp"

#include <spdlog/spdlog.h>

#include <ranges>
#include <utility>

#include "DBPFReader.h"
#include "ParseTypes.h"

DbpfIndexService::DbpfIndexService(PluginLocator locator) : locator_(std::move(locator)) {}

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
        tgiToFileIndices_.clear();
        pathToIndex_.clear();
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

auto DbpfIndexService::lookupFiles(const DBPF::Tgi& tgi) const -> std::vector<std::filesystem::path> {
    std::shared_lock lock(mutex_);
    auto it = tgiToFileIndices_.find(tgi);
    if (it == tgiToFileIndices_.end()) {
        return {};
    }
    std::vector<std::filesystem::path> result;
    result.reserve(it->second.size());
    for (const auto idx : it->second) {
        result.push_back(files_[idx]);
    }
    return result;
}

auto DbpfIndexService::containsTgi(const DBPF::Tgi& tgi) const -> bool {
    std::shared_lock lock(mutex_);
    return tgiToFileIndices_.contains(tgi);
}

auto DbpfIndexService::typeIndex() const -> const std::unordered_map<uint32_t, std::vector<DBPF::Tgi>>& {
    std::shared_lock lock(mutex_);
    return typeToTgis_;
}

auto DbpfIndexService::typeIndex(const uint32_t type) const -> std::span<const DBPF::Tgi> {
    std::shared_lock lock(mutex_);
    auto it = typeToTgis_.find(type);
    if (it != typeToTgis_.end()) {
        return it->second;
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
    // Find which file(s) contain this TGI, resolve indices to paths
    std::vector<std::filesystem::path> filePaths;
    {
        std::shared_lock readLock(mutex_);
        auto tgiIt = tgiToFileIndices_.find(tgi);
        if (tgiIt == tgiToFileIndices_.end() || tgiIt->second.empty()) {
            return Fail("TGI not found in index");
        }
        filePaths.reserve(tgiIt->second.size());
        for (const auto idx : tgiIt->second) {
            filePaths.push_back(files_[idx]);
        }
    }

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
    // Find which file(s) contain this TGI, resolve indices to paths
    std::vector<std::filesystem::path> filePaths;
    {
        std::shared_lock readLock(mutex_);
        auto tgiIt = tgiToFileIndices_.find(tgi);
        if (tgiIt == tgiToFileIndices_.end() || tgiIt->second.empty()) {
            return std::nullopt;
        }
        filePaths.reserve(tgiIt->second.size());
        for (const auto idx : tgiIt->second) {
            filePaths.push_back(files_[idx]);
        }
    }

    // Try to load from the last file that has it
    for (const auto& filePath : std::ranges::reverse_view(filePaths)) {
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
    if (!reader->LoadFile(filePath)) {
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
            for (uint32_t i = 0; i < pluginFiles.size(); ++i) {
                pathToIndex_[pluginFiles[i]] = i;
            }
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
                auto reader = std::make_unique<DBPF::Reader>();
                if (!reader->LoadFile(filePath)) {
                    spdlog::warn("Failed to load {}, not a DBPF file?", filePath.string());
                    ++errorCount_;
                    ++processedFiles_;
                    continue;
                }

                const auto& index = reader->GetIndex();

                // Accumulate entries locally, then merge under a single lock
                std::unordered_map<uint32_t, std::vector<DBPF::Tgi>> localTypeToTgis;
                std::vector<DBPF::Tgi> localTgis;
                localTgis.reserve(index.size());

                for (const auto& entry : index) {
                    if (stop_) break;
                    localTypeToTgis[entry.tgi.type].push_back(entry.tgi);
                    localTgis.push_back(entry.tgi);
                }

                {
                    std::unique_lock lock(mutex_);
                    for (auto& [type, tgis] : localTypeToTgis) {
                        auto& dest = typeToTgis_[type];
                        dest.insert(dest.end(), tgis.begin(), tgis.end());
                    }
                    auto fileIdx = pathToIndex_.at(filePath);
                    for (const auto& tgi : localTgis) {
                        tgiToFileIndices_[tgi].push_back(fileIdx);
                    }
                    readerCache_[filePath] = std::move(reader);
                    entriesIndexed_ += localTgis.size();
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

