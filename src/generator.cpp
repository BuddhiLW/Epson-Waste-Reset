#include "ewr/generator.h"
#include "ewr/protocol.h"
#include "ewr/domain.h"
#include <iostream>
#include <fstream>
#include <cstdio>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <windows.h>
#include <urlmon.h>
#else
#include <curl/curl.h>
#endif

using json = nlohmann::json;

namespace ewr {

    bool UniversalGenerator::SyncDatabaseOTA()
    {
        const char* url  = "https://raw.githubusercontent.com/RxNaison/Epson-Waste-Reset/main/database.json";
        const char* dest = "database.json";
        const char* tmp  = "database.json.tmp";

#ifdef _WIN32
        if (URLDownloadToFileA(NULL, url, tmp, 0, NULL) != S_OK)
        {
            std::remove(tmp);
            return false;
        }
#else
        CURL* curl = curl_easy_init();
        if (!curl)
            return false;

        FILE* fp = fopen(tmp, "wb");
        if (!fp)
        {
            curl_easy_cleanup(curl);
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        fclose(fp);

        if (res != CURLE_OK)
        {
            std::remove(tmp);
            return false;
        }
#endif

        {
            std::ifstream check(tmp);
            bool valid = false;
            if (check.is_open())
            {
                try
                {
                    json probe;
                    check >> probe;
                    valid = probe.is_object() && !probe.empty();
                }
                catch (const json::exception&)
                {
                    valid = false;
                }
            }
            if (!valid)
            {
                std::remove(tmp);
                return false;
            }
        }

#ifdef _WIN32
        if (!MoveFileExA(tmp, dest, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            std::remove(tmp);
            return false;
        }
#else
        if (std::rename(tmp, dest) != 0)
        {
            std::remove(tmp);
            return false;
        }
#endif
        return true;
    }

    Result<size_t> UniversalGenerator::LoadDatabase(const std::string& filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
            return Result<size_t>::Err(ErrorCode::FileNotFound, filepath + " not found");

        try
        {
            json j;
            file >> j;

            size_t loaded = 0;
            for (auto& [key, val] : j.items())
            {
                DbPrinterModel model;
                model.name = key;
                model.rkey = val.value("rkey", 0);
                model.wkey = val.value("wkey", "");

                if (val.contains("addresses") && val["addresses"].is_array())
                {
                    for (auto& addr : val["addresses"])
                        model.addresses.push_back(addr.get<uint16_t>());
                }

                if (val.contains("reset") && val["reset"].is_array())
                {
                    for (auto& rst : val["reset"])
                        model.reset_values.push_back(rst.get<uint8_t>());
                }

                while (model.reset_values.size() < model.addresses.size())
                    model.reset_values.push_back(0x00);

                database[key] = std::move(model);
                ++loaded;
            }
            return Result<size_t>::Ok(loaded);
        }
        catch (const json::exception& e)
        {
            return Result<size_t>::Err(ErrorCode::ParseFailed, e.what());
        }
    }

    bool UniversalGenerator::IsEmpty() const
    {
        return database.empty();
    }

    std::vector<DbPrinterModel> UniversalGenerator::GetAvailableModels() const
    {
        std::vector<DbPrinterModel> models;
        models.reserve(database.size());

        for (const auto& pair : database)
            models.push_back(pair.second);

        return models;
    }

    std::vector<unsigned char> UniversalGenerator::GenerateWritePacket(uint16_t rkey, uint16_t address, uint8_t value, const std::string& wkey) const
    {
        return protocol::BuildWritePacket(
            static_cast<ResetKey>(rkey),
            EepromWrite{static_cast<EepromAddress>(address), value},
            wkey);
    }

    Result<PayloadSequence> UniversalGenerator::GenerateSequence(const DbPrinterModel& model) const
    {
        if (model.addresses.empty())
            return Result<PayloadSequence>::Err(ErrorCode::EmptyPlan, "model '" + model.name + "' has no addresses to reset");
        return Result<PayloadSequence>::Ok(protocol::BuildResetSequence(model));
    }

} // namespace ewr
