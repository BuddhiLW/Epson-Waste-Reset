#include "ewr/parser.h"
#include "ewr/protocol.h"
#include <filesystem>
#include <fstream>
#include <regex>
#include <iostream>

namespace fs = std::filesystem;

namespace ewr {

    std::vector<PrinterModel> ScanModelsFolder(const std::string& folderPath)
    {
        std::vector<PrinterModel> availableModels;
        std::regex filenameRegex(R"((.+)\.(txt|c)$)");

        if (!fs::exists(folderPath))
        {
            fs::create_directory(folderPath);
            return availableModels;
        }

        for (const auto& entry : fs::directory_iterator(folderPath))
        {
            if (entry.is_regular_file())
            {
                std::string filename = entry.path().filename().string();
                std::smatch match;

                if (std::regex_match(filename, match, filenameRegex))
                {
                    PrinterModel model;
                    model.name = match[1].str();
                    model.filepath = entry.path().string();
                    availableModels.push_back(model);
                }
            }
        }
        return availableModels;
    }

    Result<PayloadSequence> ParseWiresharkDump(const std::string& filepath)
    {
        std::ifstream file(filepath);

        if (!file.is_open())
            return Result<PayloadSequence>::Err(ErrorCode::FileNotFound, "could not open payload file " + filepath);

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        PayloadSequence seq = protocol::ParseWiresharkText(content);

        if (seq.empty())
            return Result<PayloadSequence>::Err(ErrorCode::EmptyPlan, "no packets found in " + filepath);

        return Result<PayloadSequence>::Ok(std::move(seq));
    }
}
