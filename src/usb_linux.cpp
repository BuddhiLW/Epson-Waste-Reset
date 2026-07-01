#include "ewr/transport.h"
#include <libusb-1.0/libusb.h>
#include <iostream>
#include <fstream>
#include <iomanip>

namespace ewr {

    namespace {

        class LibUsbTransport : public ITransport
        {
        public:
            LibUsbTransport(libusb_device_handle* handle, int iface, unsigned char epIn, unsigned char epOut)
                : handle_(handle), iface_(iface), epIn_(epIn), epOut_(epOut)
            {
            }

            ~LibUsbTransport() override
            {
                if (handle_)
                {
                    if (iface_ >= 0)
                    {
                        libusb_release_interface(handle_, iface_);
                        libusb_attach_kernel_driver(handle_, iface_);
                    }
                    libusb_close(handle_);
                }
                libusb_exit(nullptr);
            }

            LibUsbTransport(const LibUsbTransport&) = delete;
            LibUsbTransport& operator=(const LibUsbTransport&) = delete;

            bool Send(const std::vector<unsigned char>& packet) override
            {
                int actual = 0;
                int status = libusb_bulk_transfer(handle_, epOut_,
                                                  const_cast<unsigned char*>(packet.data()),
                                                  static_cast<int>(packet.size()), &actual, 2000);
                return status == 0;
            }

            std::vector<unsigned char> Drain() override
            {
                std::vector<unsigned char> data;
                unsigned char buffer[256];
                int actual = 0;

                while (true)
                {
                    int status = libusb_bulk_transfer(handle_, epIn_, buffer, sizeof(buffer), &actual, 250);
                    if (status == LIBUSB_ERROR_TIMEOUT || actual == 0)
                        break;
                    if (status == 0)
                        data.insert(data.end(), buffer, buffer + actual);
                    else
                        break;
                }
                return data;
            }

        private:
            libusb_device_handle* handle_;
            int                   iface_;
            unsigned char         epIn_;
            unsigned char         epOut_;
        };

    } // namespace

    std::unique_ptr<ITransport> ConnectEpsonPrinter()
    {
        if (libusb_init(nullptr) < 0)
        {
            std::cerr << "Failed to initialize libusb." << std::endl;
            return nullptr;
        }

        libusb_device** devs;
        ssize_t cnt = libusb_get_device_list(nullptr, &devs);
        if (cnt < 0)
        {
            libusb_exit(nullptr);
            return nullptr;
        }

        std::ofstream logFile("ewr_trace.log", std::ios::out | std::ios::trunc);

        for (ssize_t i = 0; i < cnt; i++)
        {
            libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devs[i], &desc) < 0)
                continue;

            if (desc.idVendor != 0x04b8) // Epson VID
                continue;

            libusb_device_handle* handle = nullptr;
            if (libusb_open(devs[i], &handle) != 0)
                continue;

            libusb_config_descriptor* config = nullptr;
            libusb_get_active_config_descriptor(devs[i], &config);

            int           selected_interface = -1;
            unsigned char selected_ep_in     = 0;
            unsigned char selected_ep_out    = 0;
            bool          found_printer_class = false;

            for (int iface_idx = 0; config && iface_idx < config->bNumInterfaces; iface_idx++)
            {
                const libusb_interface_descriptor* interdesc = &config->interface[iface_idx].altsetting[0];

                if (interdesc->bInterfaceClass != LIBUSB_CLASS_PRINTER && interdesc->bInterfaceClass != LIBUSB_CLASS_VENDOR_SPEC)
                    continue;

                unsigned char ep_in  = 0;
                unsigned char ep_out = 0;

                for (int e = 0; e < interdesc->bNumEndpoints; e++)
                {
                    const libusb_endpoint_descriptor* epdesc = &interdesc->endpoint[e];
                    if ((epdesc->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK)
                    {
                        if (epdesc->bEndpointAddress & LIBUSB_ENDPOINT_IN)
                            ep_in = epdesc->bEndpointAddress;
                        else
                            ep_out = epdesc->bEndpointAddress;
                    }
                }

                if (ep_in != 0 && ep_out != 0)
                {
                    if (interdesc->bInterfaceClass == LIBUSB_CLASS_PRINTER)
                    {
                        selected_interface = iface_idx;
                        selected_ep_in     = ep_in;
                        selected_ep_out    = ep_out;
                        found_printer_class = true;
                        break;
                    }
                    else if (!found_printer_class && selected_interface == -1)
                    {
                        selected_interface = iface_idx;
                        selected_ep_in     = ep_in;
                        selected_ep_out    = ep_out;
                    }
                }
            }

            if (config)
                libusb_free_config_descriptor(config);

            if (selected_interface == -1 || selected_ep_in == 0 || selected_ep_out == 0)
            {
                libusb_close(handle);
                continue;
            }

            std::cout << "[SUCCESS] Auto-detected Epson Printer (PID: " << std::hex << std::setfill('0') << std::setw(4) << desc.idProduct << ")" << std::dec << std::endl;

            if (logFile.is_open())
            {
                logFile << "EWR Hardware Trace Log (Linux)\n";
                logFile << "==================================================\n";
                logFile << "Target VID: 0x04B8 | PID: 0x" << std::hex << desc.idProduct << std::dec << "\n";
                logFile << "Target Interface: " << selected_interface << " (Printer Class)\n";
                logFile << "Endpoint OUT: 0x" << std::hex << (int)selected_ep_out << " | Endpoint IN: 0x" << (int)selected_ep_in << std::dec << "\n";
                logFile << "==================================================\n\n";
            }

            if (libusb_kernel_driver_active(handle, selected_interface) == 1)
            {
                std::cout << "Detaching kernel driver (CUPS) for exclusive access..." << std::endl;
                libusb_detach_kernel_driver(handle, selected_interface);
            }

            if (libusb_claim_interface(handle, selected_interface) < 0)
            {
                std::cerr << "[!] Failed to claim USB interface." << std::endl;
                libusb_close(handle);
                continue;
            }

            if (logFile.is_open())
                logFile.close();
            libusb_free_device_list(devs, 1);

            return std::make_unique<LibUsbTransport>(handle, selected_interface, selected_ep_in, selected_ep_out);
        }

        if (logFile.is_open())
            logFile.close();
        libusb_free_device_list(devs, 1);
        libusb_exit(nullptr);
        return nullptr;
    }

} // namespace ewr
