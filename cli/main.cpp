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

// Turn a specific ErrorCode into an actionable hint, so a failure tells the user
// what actually went wrong instead of a generic "failed to construct payload".
static const char* errorHint(ewr::ErrorCode code)
{
    switch (code)
    {
        case ewr::ErrorCode::FileNotFound:   return "Check that the file exists and the path is correct.";
        case ewr::ErrorCode::ParseFailed:    return "The file was found but its contents could not be parsed.";
        case ewr::ErrorCode::EmptyPlan:      return "This model carries no reset data, so there is nothing to send.";
        case ewr::ErrorCode::DownloadFailed: return "OTA update failed; the local cache was left untouched.";
    }
    return "";
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

    // The database is optional: a missing/corrupt file is not fatal as long as
    // replay dumps exist, but the reason must not be silently swallowed.
    auto loadResult = generator.LoadDatabase("database.json");
    if (!loadResult.ok())
        std::cerr << "[!] Smart database unavailable: " << loadResult.error().message << std::endl;

    auto replayModels = ewr::ScanModelsFolder("models");
    auto smartModels = generator.GetAvailableModels();

    std::vector<MenuOption> options;

    // Smart models with no EEPROM addresses can never produce a write plan (they
    // fail with EmptyPlan at generation time), so they must not be offered in the
    // menu. Filtering them here is what keeps the ~113 address-less models out.
    size_t hiddenEmpty = 0;
    for (const auto& sm : smartModels)
    {
        if (sm.addresses.empty()) { ++hiddenEmpty; continue; }
        options.push_back({ sm.name + " (Smart Protocol)", false, {}, sm });
    }
    size_t smartUsable = options.size();

    for (const auto& lm : replayModels)
        options.push_back({ lm.name + " (Replay)", true, lm, {} });

    if (options.empty())
    {
        std::cerr << "\n[!] No usable payloads found. You need internet access on first run, or a 'models' folder with payload dumps." << std::endl;
        std::cin.get();
        return 1;
    }

    std::cout << "[i] Loaded " << smartUsable << " Smart Protocol payloads";
    if (hiddenEmpty)
        std::cout << " (" << hiddenEmpty << " address-less models hidden)";
    std::cout << "." << std::endl;
    std::cout << "[i] Loaded " << replayModels.size() << " Custom payloads.\n" << std::endl;

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

    ewr::PayloadSequence executionSequence;

    if (selected.isReplay)
    {
        std::cout << "\n[!] Parsing replay Wireshark dump for " << selected.displayName << "..." << std::endl;
        auto parsed = ewr::ParseWiresharkDump(selected.replayModel.filepath);
        if (!parsed.ok())
        {
            std::cerr << "[-] " << parsed.error().message << "\n    " << errorHint(parsed.error().code) << std::endl;
            std::cin.get();
            return 1;
        }
        executionSequence = std::move(parsed.value());
    }
    else
    {
        std::cout << "\n[*] Generating safe Smart Protocol R/W sequence for " << selected.displayName << "..." << std::endl;
        auto generated = generator.GenerateSequence(selected.smartModel);
        if (!generated.ok())
        {
            std::cerr << "[-] " << generated.error().message << "\n    " << errorHint(generated.error().code) << std::endl;
            std::cin.get();
            return 1;
        }
        executionSequence = std::move(generated.value());
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