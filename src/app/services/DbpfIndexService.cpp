#include "DbpfIndexService.hpp"

DbpfIndexService::DbpfIndexService(const PluginLocator& locator) : locator_(locator) {}

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
        std::lock_guard lock(mutex_);
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

auto DbpfIndexService::snapshot() const -> ScanProgress {

}
