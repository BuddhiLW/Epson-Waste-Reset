#pragma once
#include <memory>
#include <vector>

namespace ewr {

    struct ITransport
    {
        virtual ~ITransport() = default;

        virtual bool Send(const std::vector<unsigned char>& packet) = 0;

        virtual std::vector<unsigned char> Drain() = 0;
    };

    std::unique_ptr<ITransport> ConnectEpsonPrinter();

} // namespace ewr
