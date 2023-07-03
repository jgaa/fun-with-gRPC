
#include <iostream>
#include <filesystem>
#include <boost/program_options.hpp>

#include "route_guide.grpc.pb.h"
#include <glad/grpc.hpp>

using namespace std;

void doServer() {

    // Declare the service
    using server_t = ::jgaa::glad::GrpcServer<routeguide::RouteGuide>;
    server_t server;

    // Add handlers. Each handler provides one callback.
    // Together, the handlers should cover all the defined functions one grpc service.
    server.addUnary<routeguide::Point, routeguide::Feature>([] () {
        clog << "In unary cb" << endl;
    });

    // Start the service.
    server.start();
}

int main(int argc, char* argv[]) {
    try {
        locale loc("");
    } catch (const std::exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    std::string mode = "server";

    namespace po = boost::program_options;
    po::options_description general("Options");
    po::positional_options_description positionalDescription;

    general.add_options()
        ("help,h", "Print help and exit")
        ("mode,m",
         po::value<>(&mode)->default_value(mode),
         "Mode. Either \"server\" or \"client\"")
//        ("log-to-console,C",
//         po::value<string>(&log_level_console)->default_value(log_level_console),
//         "Log-level to the consolee; one of 'info', 'debug', 'trace'. Empty string to disable.")
//        ("log-level,l",
//         po::value<string>(&log_level)->default_value(log_level),
//         "Log-level; one of 'info', 'debug', 'trace'.")
//        ("log-file,L",
//         po::value<string>(&log_file),
//         "Log-file to write a log to. Default is to use only the console.")
//        ("truncate-log-file,T",
//         po::value<bool>(&trunc_log)->default_value(trunc_log),
//         "Log-file to write a log to. Default is to use the console.")
//        ("reset-auth",
//         "Resets the 'admin' account and the 'nsBLAST' tenant to it's default, initial state."
//         "The server will terminate after the changes are made.")
        ;

    po::options_description cmdline_options;
    cmdline_options.add(general);
    po::positional_options_description kfo;
    kfo.add("kubeconfig", -1);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(kfo).run(), vm);
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

    clog << filesystem::path(argv[0]).stem().string() << " starting up as " << mode << ". " << endl;

    if (mode == "server") {
        try {
            doServer();
        } catch (const exception& ex) {
            cerr << "Caught exception from Server: " << ex.what() << endl;
        }
    } else if (mode == "client") {

    }

    clog << filesystem::path(argv[0]).stem().string() << " done! " << endl;
} // mail
