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

#include "callback-client-impl.hpp"
#include "funwithgrpc/Config.h"
#include "funwithgrpc/logging.h"

using namespace std;

namespace {

Config config;

template <typename T>
void runClient() {
    T client(config);

    // Unlike the server, here we use the main-thread to run the event-loop.
    // run() returns when we are finished with the requests.
    client.run();
}

void process() {
//    if (client_type == "first") {
//        runClient<SimpleReqResClient>();
//    } else if (client_type == "second") {
//        runClient<UnaryAndSingleStreamClient>();
//    } else if (client_type == "third") {
//        runClient<EverythingClient>();
//    } else {
//        throw runtime_error{"Unknows client-type: "s + client_type};
//    }

    runClient<EverythingCallbackClient>();
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
        ("version,v", "Print version and exit")
        ("address,a",
         po::value(&config.address)->default_value(config.address),
         "Network address to use for gRPC.")
        ("request-type,t",
         // Ugly, but valid.
         po::value(reinterpret_cast<int *>(&config.request_type))
             ->default_value(static_cast<int>(config.request_type)),
         "Reqest to send:\n   0=GetFeature\n   1=ListFeatures\n   2=RecordRoute\n   3=RouteChat")
        ("log-to-console,C",
         po::value(&log_level_console)->default_value(log_level_console),
         "Log-level to the console; one of 'info', 'debug', 'trace'. Empty string to disable.")
        ("num-requests,r",
         po::value(&config.num_requests)->default_value(config.num_requests),
         "Total number of requests to send.")
        ("parallel-requests,p",
         po::value(&config.parallel_requests)->default_value(config.parallel_requests),
         "Number of requests to send in parallel.")
        ("stream-messages,s",
         po::value(&config.num_stream_messages)->default_value(config.num_stream_messages),
         "Number of messages to send in a stream (for requests with an outgoing stream).")
        ("queue-work-around,q",
         po::value(&config.do_push_back_on_queue)->default_value(config.do_push_back_on_queue),
         "Work-around to put all async operations at the end of the qork-queue.")
        ;

    const auto appname = filesystem::path(argv[0]).stem().string();
    po::options_description cmdline_options;
    cmdline_options.add(general);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).run(), vm);
        po::notify(vm);
    } catch (const std::exception& ex) {
        cerr << appname
             << " Failed to parse command-line arguments: " << ex.what() << endl;
        return -1;
    }

    if (vm.count("help")) {
        std::cout << appname << " [options]";
        std::cout << cmdline_options << std::endl;
        return -2;
    }

    if (vm.count("version")) {
        std::cout << appname << ' ' << VERSION << endl
                  << "Using C++ standard " << __cplusplus << endl
                  << "Platform " << BOOST_PLATFORM << endl
                  << "Compiler " << BOOST_COMPILER << endl
                  << "Build date " <<__DATE__ << endl;
        return -3;
    }

    if (auto level = toLogLevel(log_level_console)) {
        logfault::LogManager::Instance().AddHandler(
            make_unique<logfault::StreamHandler>(clog, *level));
    }

    LOG_INFO << appname << " starting up.";

    try {
        process();
    } catch (const exception& ex) {
        cerr << "Caught exception from process(): " << ex.what() << endl;
    }

    LOG_INFO << appname << " done! ";
} // main
