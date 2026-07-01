#include "ewr/generator.h"
#include "ewr/protocol.h"
#include "ewr/domain.h"
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

#ifdef _WIN32
#include <urlmon.h>
#else
#include <curl/curl.h>
#endif

using json = nlohmann::json;

namespace ewr {

    bool UniversalGenerator::SyncDatabaseOTA()
    {
        const char* url = "https://raw.githubusercontent.com/RxNaison/Epson-Waste-Reset/main/database.json";
        const char* dest = "database.json";

#ifdef _WIN32
        HRESULT res = URLDownloadToFileA(NULL, url, dest, 0, NULL);
        return (res == S_OK);
#else
        CURL* curl = curl_easy_init();
        if (curl)
        {
            FILE* fp = fopen(dest, "wb");
            curl_easy_setopt(curl, CURLOPT_URL, url);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            fclose(fp);
            return (res == CURLE_OK);
        }
        return false;
#endif
    }

    bool UniversalGenerator::LoadDatabase(const std::string& filepath)
    {
        std::ifstream file(filepath);
        if (!file.is_open())
            return false;

        try
        {
            json j;
            file >> j;

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
            }
            return true;
        }
        catch (const json::exception& e)
        {
            std::cerr << "[!] JSON Parse Error: " << e.what() << std::endl;
            return false;
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

    // Thin delegators to the pure protocol core (ewr/protocol.h). The byte
    // construction now lives in src/protocol.cpp so it can be tested without I/O.
    std::vector<unsigned char> UniversalGenerator::GenerateWritePacket(uint16_t rkey, uint16_t address, uint8_t value, const std::string& wkey) const
    {
        return protocol::BuildWritePacket(
            static_cast<ResetKey>(rkey),
            EepromWrite{static_cast<EepromAddress>(address), value},
            wkey);
    }

    std::vector<std::vector<unsigned char>> UniversalGenerator::GenerateSequence(const DbPrinterModel& model) const
    {
        return protocol::BuildResetSequence(model);
    }

} // namespace ewr
