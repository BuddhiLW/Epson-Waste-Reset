#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <cctype>
#include "ewr/payload.h"
#include "ewr/parser.h"
#include "ewr/transport.h"
#include "ewr/executor.h"
#include "ewr/generator.h"

struct MenuOption
{
    std::string displayName;
    bool isReplay;
    ewr::PrinterModel replayModel;
    ewr::DbPrinterModel smartModel;
};

std::string toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) { return std::tolower(c); });
    return str;
}

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

    MenuOption selected;
    bool hasSelected = false;

    while (!hasSelected)
    {
        std::cout << "\nEnter printer model to search (e.g., 'L3150' or 'XP') or type 'exit' to quit: ";
        std::string searchQuery;
        std::getline(std::cin, searchQuery);

        if (searchQuery.empty())
            continue;

        std::string searchLower = toLower(searchQuery);
        if (searchLower == "exit" || searchLower == "quit")
            return 0;

        std::vector<MenuOption> filteredOptions;
        for (const auto& opt : options)
        {
            if (toLower(opt.displayName).find(searchLower) != std::string::npos)
                filteredOptions.push_back(opt);
        }

        if (filteredOptions.empty())
        {
            std::cout << "[-] No printers found matching '" << searchQuery << "'. Please try again.\n";
            continue;
        }

        std::cout << "\nFound " << filteredOptions.size() << " matching printers:\n";

        for (size_t i = 0; i < filteredOptions.size(); ++i)
            std::cout << "[" << i + 1 << "] " << filteredOptions[i].displayName << "\n";

        std::cout << "[0] Search again...\n";

        std::cout << "\nSelect your printer [0-" << filteredOptions.size() << "]: ";
        std::string choiceStr;
        std::getline(std::cin, choiceStr);

        try
        {
            int choice = std::stoi(choiceStr);
            if (choice == 0)
            {
                continue;
            }
            else if (choice >= 1 && choice <= static_cast<int>(filteredOptions.size()))
            {
                selected = filteredOptions[choice - 1];
                hasSelected = true;
            }
            else
            {
                std::cout << "[-] Invalid selection. Please try again.\n";
            }
        }
        catch (...)
        {
            std::cout << "[-] Invalid input. Please enter a number.\n";
        }
    }

    std::vector<std::vector<unsigned char>> executionSequence;

    if (selected.isReplay)
    {
        std::cout << "\n[!] Parsing replay Wireshark dump for " << selected.displayName << "..." << std::endl;
        executionSequence = ewr::ParseWiresharkDump(selected.replayModel.filepath);
    }
    else
    {
        std::cout << "\n[*] Generating safe Smart Protocol R/W sequence for " << selected.displayName << "..." << std::endl;
        executionSequence = generator.GenerateSequence(selected.smartModel);
    }

    if (executionSequence.empty())
    {
        std::cerr << "[-] Failed to construct payload. Exiting.\n";
        std::cin.get();
        return 1;
    }

    std::cout << "Scanning USB ports for Epson device..." << std::endl;
    std::unique_ptr<ewr::ITransport> transport = ewr::ConnectEpsonPrinter();

    if (!transport)
    {
        std::cerr << "[ERROR] Could not find an Epson printer. Is it turned on and plugged in?" << std::endl;
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "\nExecuting universal hardware state machine..." << std::endl;
    std::cout << "[i] Saving hardware trace to ewr_trace.log for diagnostics." << std::endl;

    std::ofstream trace("ewr_trace.log", std::ios::app);
    ewr::ProtocolExecutor executor(*transport, trace.is_open() ? &trace : nullptr);
    ewr::ExecutionResult result = executor.Run(executionSequence);
    trace.close();

    if (result.success)
    {
        std::cout << "\n========================================" << std::endl;
        std::cout << " SUCCESS! Turn the printer OFF, then ON." << std::endl;
        std::cout << "========================================" << std::endl;
    }
    else
    {
        std::cerr << "\n[ERROR] The reset was NOT confirmed by the printer." << std::endl;
        if (result.sendError)
            std::cerr << "    A packet failed to transmit over USB." << std::endl;
        else if (result.writesRejected > 0)
            std::cerr << "    The printer REJECTED " << result.writesRejected << " of " << result.writesTotal << " EEPROM writes (:42:NG;)." << std::endl;
        else
            std::cerr << "    Only " << result.writesAcked << " of " << result.writesTotal << " EEPROM writes were acknowledged (:42:OK;)." << std::endl;
        std::cerr << "[!] Diagnostic tips:" << std::endl;
        std::cerr << "    1. Unplug the printer's USB cable, wait 5 seconds, and plug it back in." << std::endl;
        std::cerr << "    2. Restart the printer and try again." << std::endl;
        std::cerr << "    3. Ensure no other printing software (like CUPS or Epson Status Monitor) is active." << std::endl;
    }

    // RAII: transport's destructor releases the interface, reattaches the kernel
    // driver, and tears libusb down. No explicit DisconnectPrinter.
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get();
    return result.success ? 0 : 1;
}