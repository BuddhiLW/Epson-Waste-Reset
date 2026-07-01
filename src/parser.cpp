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

    // Thin I/O wrapper: read the file, then delegate to the pure parser
    // (ewr::protocol::ParseWiresharkText) so the byte logic is unit-testable.
    std::vector<std::vector<unsigned char>> ParseWiresharkDump(const std::string& filepath)
    {
        std::ifstream file(filepath);

        if (!file.is_open())
        {
            std::cerr << "Error: Could not open payload file." << std::endl;
            return {};
        }

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return protocol::ParseWiresharkText(content);
    }
}
