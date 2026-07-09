// Oracle: compiled against upstream's OWN generator.{h,cpp} (fetched by
// tests/verify_against_upstream.sh). Emits "<name>\t<hex packets>" per model.
// Uses upstream API only: bool LoadDatabase / GetAvailableModels / GenerateSequence.

#include "ewr/generator.h"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    const std::string dbpath = (argc > 1) ? argv[1] : "database.json";

    ewr::UniversalGenerator gen;
    if (!gen.LoadDatabase(dbpath))
    {
        std::cerr << "upstream dumper: LoadDatabase failed\n";
        return 2;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto& m : gen.GetAvailableModels())
    {
        if (m.addresses.empty())
            continue;

        auto seq = gen.GenerateSequence(m);

        out << m.name << '\t';
        for (size_t p = 0; p < seq.size(); ++p)
        {
            if (p) out << '|';
            for (unsigned char b : seq[p])
                out << std::setw(2) << static_cast<int>(b);
        }
        out << '\n';
    }
    std::cout << out.str();
    return 0;
}
