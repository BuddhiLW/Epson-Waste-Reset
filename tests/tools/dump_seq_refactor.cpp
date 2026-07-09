// Refactored core -> "<name>\t<hex packets, '|'-separated>" per model.
// Oracle-paired with dump_seq_upstream.cpp; driver: tests/verify_against_upstream.sh

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
    auto loaded = gen.LoadDatabase(dbpath);
    if (!loaded)
    {
        std::cerr << "refactor dumper: LoadDatabase failed: " << loaded.error().message << "\n";
        return 2;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto& m : gen.GetAvailableModels())
    {
        if (m.addresses.empty())
            continue;

        auto res = gen.GenerateSequence(m);
        if (!res)
            continue;
        const auto& seq = res.value();

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
