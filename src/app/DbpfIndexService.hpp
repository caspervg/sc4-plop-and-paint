#pragma once
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "DBPFReader.h"
#include "ExemplarReader.h"
#include "PluginLocator.hpp"
#include "TGI.h"

struct ScanProgress {
    size_t totalFiles = 0;
    size_t processedFiles = 0;
    size_t entriesIndexed = 0;
    size_t errorCount = 0;
    std::string currentFile;
    bool done = false;
};

class DbpfIndexService {
public:
    explicit DbpfIndexService(PluginLocator  locator);
    ~DbpfIndexService();

    auto start() -> void;
    auto shutdown() -> void;

    [[nodiscard]] auto isRunning() const -> bool;
    [[nodiscard]] auto snapshot() const -> ScanProgress;
    [[nodiscard]] auto tgiIndex() const -> const std::unordered_map<DBPF::Tgi, std::vector<std::filesystem::path>, DBPF::TgiHash>&;
    [[nodiscard]] auto typeInstanceIndex() const -> const std::unordered_map<uint64_t, std::vector<DBPF::Tgi>>&;
    auto typeIndex() const -> const std::unordered_map<uint32_t, std::vector<DBPF::Tgi>>&;
    auto typeIndex(uint32_t type) -> std::vector<DBPF::Tgi>;
    [[nodiscard]] auto dbpfFiles() const -> const std::vector<std::filesystem::path>&;
    [[nodiscard]] auto pluginLocator() const -> const PluginLocator&;

    // Load an exemplar by TGI using cached readers
    // Returns a pointer to the cached exemplar (stays valid until shutdown)
    [[nodiscard]] auto loadExemplar(const DBPF::Tgi& tgi) const -> ParseExpected<const Exemplar::Record*>;

    // Load raw entry data by TGI using cached readers
    [[nodiscard]] auto loadEntryData(const DBPF::Tgi& tgi) const -> std::optional<std::vector<uint8_t>>;

    // Get or create a cached reader for a specific file
    [[nodiscard]] auto getReader(const std::filesystem::path& filePath) const -> DBPF::Reader*;

private:
    auto worker_() -> void;
    auto publishProgress_() -> void;

private:
    PluginLocator locator_;

    mutable std::shared_mutex mutex_;
    std::thread workerThread_;
    std::atomic_bool running_{false};
    std::atomic_bool stop_{false};
    std::atomic_bool done_{false};
    std::atomic<size_t> totalFiles_{0};
    std::atomic<size_t> processedFiles_{0};
    std::atomic<size_t> entriesIndexed_{0};
    std::atomic<size_t> errorCount_{0};

    std::string currentFile_;
    std::vector<std::filesystem::path> files_;
    std::unordered_map<DBPF::Tgi, std::vector<std::filesystem::path>, DBPF::TgiHash> tgiToFiles_;
    std::unordered_map<uint32_t, std::vector<DBPF::Tgi>> typeToTgis_;
    std::unordered_map<uint64_t, std::vector<DBPF::Tgi>> typeInstanceToTgis_;

    // Cache of DBPF readers (one per file) for fast exemplar loading
    mutable std::unordered_map<std::filesystem::path, std::unique_ptr<DBPF::Reader>> readerCache_;

    // Cache of loaded exemplars
    mutable std::unordered_map<DBPF::Tgi, Exemplar::Record, DBPF::TgiHash> exemplarCache_;
};
