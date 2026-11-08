#include "PackCatalogBuilder.hpp"

#include <chrono>
#include <cstdio>
#include <format>
#include <map>
#include <set>
#include <string>
#include <unordered_map>

#include <curl/curl.h>
#include <curl/header.h>
#include <sqlite3.h>

namespace fs = std::filesystem;

namespace {
    constexpr const char* kCatalogUrl =
        "https://github.com/noah-severyn/SC4PropTextureCatalog/raw/main/SC4PropTextureCatalogAPI/data/Catalog.db";
    constexpr const char* kSource = "SC4 Prop Texture Catalog by Noah Severyn, "
                                    "https://github.com/noah-severyn/SC4PropTextureCatalog";
    constexpr uint32_t kTextureType = 0x7AB50E44;
    constexpr uint32_t kTextureGroup = 0x0986135E;
    constexpr uint32_t kExemplarType = 0x6534284A;

    std::string NowUtc() {
        return std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    }

    size_t WriteToFile(const char* data, const size_t size, const size_t count, void* file) {
        return std::fwrite(data, size, count, static_cast<std::FILE*>(file));
    }

    enum class Fetched { Downloaded, NotModified, Failed };

    // Downloads url to target, unless its ETag is still etag; then the new ETag is in etag.
    Fetched Download(const char* url, const fs::path& target, std::string& etag, spdlog::logger& logger) {
        std::FILE* file = nullptr;
#ifdef _WIN32
        _wfopen_s(&file, target.c_str(), L"wb");
#else
        file = std::fopen(target.c_str(), "wb");
#endif
        if (!file) {
            logger.error("Cannot write {}", target.string());
            return Fetched::Failed;
        }

        curl_global_init(CURL_GLOBAL_DEFAULT);
        CURL* curl = curl_easy_init();
        CURLcode result = CURLE_FAILED_INIT;
        long status = 0;
        curl_slist* headers = nullptr;
        if (curl) {
            if (!etag.empty()) {
                headers = curl_slist_append(headers, ("If-None-Match: " + etag).c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            }
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "SC4PlopAndPaint-CLI/" SC4_PLOP_AND_PAINT_VERSION);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
            // A slow link may take a while for the ~20 MB, but should not hang forever.
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 60L);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
#ifndef _WIN32
            // OpenSSL is linked in statically and would look for certificates where it was built;
            // use the distribution's bundle. Windows uses its own certificate store.
            for (const char* bundle : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                                       "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem"}) {
                std::error_code ec;
                if (fs::exists(bundle, ec)) {
                    curl_easy_setopt(curl, CURLOPT_CAINFO, bundle);
                    break;
                }
            }
#endif
            result = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            // The last response's, after GitHub's redirect to its raw file host.
            curl_header* header = nullptr;
            if (result == CURLE_OK && curl_easy_header(curl, "ETag", 0, CURLH_HEADER, -1, &header) == CURLHE_OK) {
                etag = header->value;
            }
            curl_easy_cleanup(curl);
        }
        curl_slist_free_all(headers);
        curl_global_cleanup();
        std::fclose(file);

        if (result != CURLE_OK) {
            logger.warn("Could not download the pack catalog: {}", curl_easy_strerror(result));
            return Fetched::Failed;
        }
        return status == 304 ? Fetched::NotModified : Fetched::Downloaded;
    }

    // One prepared statement, stepped row by row.
    class Query {
    public:
        Query(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &statement_, nullptr); }
        ~Query() { sqlite3_finalize(statement_); }
        Query(const Query&) = delete;
        Query& operator=(const Query&) = delete;

        [[nodiscard]] bool Valid() const { return statement_ != nullptr; }
        bool Next() { return statement_ && sqlite3_step(statement_) == SQLITE_ROW; }
        [[nodiscard]] std::string Text(const int column) const {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement_, column));
            return text ? text : "";
        }
        [[nodiscard]] int64_t Int(const int column) const { return sqlite3_column_int64(statement_, column); }

    private:
        sqlite3_stmt* statement_ = nullptr;
    };

    std::string FileStem(std::string path) {
        for (char& c : path) {
            if (c == '\\') {
                c = '/';
            }
        }
        return fs::path(path).stem().string();
    }
}

namespace PackCatalogBuilder {
    std::optional<PackCatalog> Read(const fs::path& databasePath, spdlog::logger& logger) {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(databasePath.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            logger.warn("Could not open the pack catalog {}: {}", databasePath.string(), db ? sqlite3_errmsg(db) : "");
            sqlite3_close(db);
            return std::nullopt;
        }

        PackCatalog catalog;
        catalog.format = kPackCatalogFormat;
        catalog.source = kSource;
        bool ok = true;
        {
            Query newest(db, "select max(LastModified) from Assets");
            ok = newest.Valid();
            if (newest.Next()) {
                catalog.catalogUpdatedAt = newest.Text(0);
            }

            // The first sc4pac package of each file, for its id and author.
            std::unordered_map<int64_t, std::pair<std::string, std::string>> packageOfFile;
            Query packages(db, "select pf.FileId, p.Name, p.Author from PackageFiles pf "
                               "join Packages p on p.Id = pf.PackageId order by pf.FileId, p.Id");
            ok = ok && packages.Valid();
            while (packages.Next()) {
                packageOfFile.try_emplace(packages.Int(0), packages.Text(1), packages.Text(2));
            }

            // The catalog's TGICategories: 1 is Prop, 2 is Texture, 4 is Flora.
            Query items(db, "select t.TGI, t.Category, t.FileId, f.Name, a.Name, a.Url from TGIs t "
                            "join Files f on f.Id = t.FileId left join Assets a on a.Id = f.AssetId "
                            "where t.Category in (1, 2, 4)");
            ok = ok && items.Valid();
            std::map<std::string, uint32_t> packIndex;
            std::map<std::string, uint32_t> fileIndex;

            // The pack and file of the current row, added to the lists when new.
            const auto packOf = [&]() {
                const auto package = packageOfFile.find(items.Int(2));
                std::string packName = items.Text(4);
                if (packName.empty()) {
                    packName = package != packageOfFile.end() && !package->second.first.empty() ? package->second.first
                                                                                                 : "(unknown)";
                }
                auto [pack, newPack] = packIndex.try_emplace(packName, static_cast<uint32_t>(catalog.packs.size()));
                if (newPack) {
                    std::string url = items.Text(5);
                    for (char& c : url) {
                        if (c == '\\') {
                            c = '/';
                        }
                    }
                    catalog.packs.push_back(TexturePack{
                        .name = packName,
                        .package = package != packageOfFile.end() ? package->second.first : "",
                        .author = package != packageOfFile.end() ? package->second.second : "",
                        .url = std::move(url),
                    });
                }
                return pack->second;
            };
            const auto fileOf = [&]() {
                auto [file, newFile] = fileIndex.try_emplace(FileStem(items.Text(3)),
                                                             static_cast<uint32_t>(catalog.files.size()));
                if (newFile) {
                    catalog.files.push_back(file->first);
                }
                return file->second;
            };

            // Every pack that ships an item: mods often override the same texture or prop. Sets keep
            // the rows sorted by item and drop exact repeats.
            std::set<std::array<uint32_t, 3>> textureRows;
            std::set<std::array<uint32_t, 4>> exemplarRows;
            while (ok && items.Next()) {
                uint32_t type = 0;
                uint32_t group = 0;
                uint32_t instance = 0;
                if (std::sscanf(items.Text(0).c_str(), "%x, %x, %x", &type, &group, &instance) != 3) {
                    continue;
                }
                if (items.Int(1) != 2) {
                    // A prop or flora exemplar, known by its group and instance as in props.cbor and
                    // flora.cbor.
                    if (type == kExemplarType) {
                        exemplarRows.insert({group, instance, fileOf(), packOf()});
                    }
                    continue;
                }
                // The catalog lists zoom 1, ending in 0, 5 or A; Plop and Paint lists zoom 5, 4 more.
                if (type != kTextureType || group != kTextureGroup ||
                    ((instance & 0xF) != 0x0 && (instance & 0xF) != 0x5 && (instance & 0xF) != 0xA)) {
                    continue;
                }
                textureRows.insert({instance + 4, fileOf(), packOf()});
            }
            catalog.textures.assign(textureRows.begin(), textureRows.end());
            catalog.exemplars.assign(exemplarRows.begin(), exemplarRows.end());
        }
        if (!ok) {
            logger.warn("The pack catalog {} is not in the expected form: {}", databasePath.string(), sqlite3_errmsg(db));
        }
        sqlite3_close(db);
        if (!ok) {
            return std::nullopt;
        }
        logger.info("Pack catalog: {} textures and {} props and flora in {} packs, updated up to {}",
                    catalog.textures.size(), catalog.exemplars.size(), catalog.packs.size(), catalog.catalogUpdatedAt);
        return catalog;
    }

    std::optional<PackCatalog> FetchLatest(const fs::path& workDir, const PackCatalog* current, spdlog::logger& logger) {
        const fs::path databasePath = workDir / "packs-catalog.db.tmp";
        logger.info("Fetching the latest pack catalog from {}", kCatalogUrl);
        // A file from an older cache builder lacks what this one adds: fetch it all again.
        std::string etag = current && current->format == kPackCatalogFormat ? current->etag : "";
        std::optional<PackCatalog> catalog;
        switch (Download(kCatalogUrl, databasePath, etag, logger)) {
        case Fetched::NotModified:
            logger.info("The pack catalog has not changed since {}", current->fetchedAt);
            catalog = *current;
            break;
        case Fetched::Downloaded:
            catalog = Read(databasePath, logger);
            if (catalog) {
                catalog->etag = etag;
            }
            break;
        case Fetched::Failed:
            break;
        }
        if (catalog) {
            catalog->fetchedAt = NowUtc();
        }
        std::error_code ec;
        fs::remove(databasePath, ec);
        return catalog;
    }
}
