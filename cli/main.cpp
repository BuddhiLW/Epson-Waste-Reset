#include <iostream>
#include <algorithm>
#include "ewr/payload.h"
#include "ewr/parser.h"
#include "ewr/usb.h"
#include "ewr/generator.h"

struct MenuOption
{
    std::string displayName;
    bool isReplay;
    ewr::PrinterModel replayModel;
    ewr::DbPrinterModel smartModel;
};

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "       EWR - Epson Waste Reset          " << std::endl;
    std::cout << "========================================\n" << std::endl;

    ewr::UniversalGenerator generator;

    std::cout << "[i] Checking for OTA database updates... ";

    if (generator.SyncDatabaseOTA())
        std::cout << "SUCCESS." << std::endl;
    else
        std::cout << "OFFLINE (Using local cache)." << std::endl;

    generator.LoadDatabase("database.json");
    auto replayModels = ewr::ScanModelsFolder("models");
    auto smartModels = generator.GetAvailableModels();

    if (replayModels.empty() && smartModels.empty())
    {
        std::cerr << "\n[!] No payloads found. You need internet access on first run, or a 'models' folder with payload dumps." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "[i] Loaded " << smartModels.size() << " Smart Protocol payloads." << std::endl;
    std::cout << "[i] Loaded " << replayModels.size() << " Custom payloads.\n" << std::endl;

    std::vector<MenuOption> options;

    for (const auto& sm : smartModels)
        options.push_back({ sm.name + " (Smart Protocol)", false, {}, sm });

    for (const auto& lm : replayModels)
        options.push_back({ lm.name + " (Replay)", true, lm, {} });

    std::sort(options.begin(), options.end(), [](const MenuOption& a, const MenuOption& b)
        {
            return a.displayName < b.displayName;
        });

    std::cout << "Available Printer Payloads:\n";

    for (size_t i = 0; i < options.size(); ++i)
        std::cout << "[" << i + 1 << "] " << options[i].displayName << "\n";

    int choice;
    std::cout << "\nSelect your printer: ";
    std::cin >> choice;

    std::cin.clear();
    std::cin.ignore(256, '\n');

    if (choice < 1 || choice > static_cast<int>(options.size()))
    {
        std::cout << "Invalid selection. Exiting.\n";
        std::cin.get();
        return 1;
    }

    MenuOption selected = options[choice - 1];
    std::vector<std::vector<unsigned char>> executionSequence;

    if (selected.isReplay)
    {
        std::cout << "\n[!] Parsing replay Wireshark dump..." << std::endl;
        executionSequence = ewr::ParseWiresharkDump(selected.replayModel.filepath);
    }
    else
    {
        std::cout << "\n[*] Generating safe Smart Protocol R/W sequence..." << std::endl;
        executionSequence = generator.GenerateSequence(selected.smartModel);
    }

    if (executionSequence.empty())
    {
        std::cerr << "Failed to construct payload. Exiting.\n";
        std::cin.get();
        return 1;
    }

    std::cout << "\nScanning USB ports for Epson device..." << std::endl;
    ewr::EwrDeviceHandle hPrinter = ewr::AutoConnectEpsonPrinter();

    if (!hPrinter)
    {
        std::cerr << "[ERROR] Could not find an Epson printer. Is it turned on and plugged in?" << std::endl;
        std::cin.get();
        return 1;
    }

    if (ewr::ExecutePayloadSequence(hPrinter, executionSequence))
    {
        std::cout << "\n========================================" << std::endl;
        std::cout << " SUCCESS! Turn the printer OFF, then ON." << std::endl;
        std::cout << "========================================" << std::endl;
    }

    ewr::DisconnectPrinter(hPrinter);
    std::cin.get();
    return 0;
}