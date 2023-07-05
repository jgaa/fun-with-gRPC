/* This is an experiment where I play with gRPC for C++.
 *
 * This is an example of a possible server-implementation, using
 * the gRPC "Async" interface that is generated with the protobuf
 * utility.
 *
 * The protofile is the same that Google use in their general documentation
 * for gRPC.
 *
 *
 * This file is free and open source code, released under the
 * GNU GENERAL PUBLIC LICENSE version 3.
 *
 * Copyright 2023 by Jarle (jgaa) Aase. All rights reserved.
 */

#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>

#include "simple-req-res.hpp"

#include "route_guide.grpc.pb.h"
#include "funwithgrpc/logging.h"

using namespace std;

namespace {

// The gRPC address we will use
std::string address = "127.0.0.1:10123";

template <typename T>
void handleSignals(auto& signals, bool& done, T& service) {
    signals.async_wait([&](const boost::system::error_code& ec, int signalNumber) {

        if (ec) {
            if (ec == boost::asio::error::operation_aborted) {
                LOG_TRACE << "handleSignals: Handler aborted.";
                return;
            }
            LOG_WARN << "handleSignals - Received error: " << ec.message();
            return;
        }

        LOG_INFO << "handleSignals - Received signal #" << signalNumber;
        if (signalNumber == SIGHUP) {
            LOG_WARN << "handleSignals - Ignoring SIGHUP. Note - config is not re-loaded.";
        } else if (signalNumber == SIGQUIT || signalNumber == SIGINT) {
            if (!done) {
                LOG_INFO << "handleSignals - Stopping the service.";
                service.stop();
                done = true;
            }
            return;
        } else {
            LOG_WARN << "handleSignals - Ignoring signal #" << signalNumber;
        }

        handleSignals(signals, done, service);
    });
}

void process() {
    SimpleReqRespSvc svc;
    svc.run(address);

    // Let's allow the suer to exit the server with ctl-C
    boost::asio::io_context ctx;
    boost::asio::signal_set signals{ctx, SIGINT, SIGQUIT, SIGHUP};

    bool done = false;
    handleSignals(signals, done, svc);

    // Use the main thread to run asio's event-loop.
    // run() will return when the signal-handler is done.
    ctx.run();
}

} // anon ns

int main(int argc, char* argv[]) {
    try {
        locale loc("");
    } catch (const std::exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    namespace po = boost::program_options;
    po::options_description general("Options");
    std::string log_level_console = "info";

    general.add_options()
        ("help,h", "Print help and exit")
        ("address,a",
         po::value(&address)->default_value(address),
         "Network address to use for gRPC.")
        ("log-to-console,C",
         po::value(&log_level_console)->default_value(log_level_console),
         "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
        ;

    po::options_description cmdline_options;
    cmdline_options.add(general);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << filesystem::path(argv[0]).stem().string()
             << " Failed to parse command-line arguments: " << ex.what() << endl;
        return -1;
    }

    if (vm.count("help")) {
        std::cout << filesystem::path(argv[0]).stem().string() << " [options]";
        std::cout << cmdline_options << std::endl;
        return -2;
    }

    if (auto level = toLogLevel(log_level_console)) {
        logfault::LogManager::Instance().AddHandler(
            make_unique<logfault::StreamHandler>(clog, *level));
    }

    const auto my_name = filesystem::path(argv[0]).stem().string();
    LOG_INFO << my_name << " starting up.";

    try {
        process();
    } catch (const exception& ex) {
        cerr << "Caught exception from process: " << ex.what() << endl;
    }

    LOG_INFO << my_name << " done! ";
} // main
