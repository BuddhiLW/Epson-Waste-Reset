#pragma once
#include "ewr/payload.h"
#include "ewr/domain.h"
#include "ewr/result.h"
#include <string>
#include <vector>

namespace ewr {
    std::vector<PrinterModel> ScanModelsFolder(const std::string& folderPath);
    Result<PayloadSequence> ParseWiresharkDump(const std::string& filepath);
}