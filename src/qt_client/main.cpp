#include <filesystem>
#include <iostream>
#include <string_view>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQmlContext>
#include <QCommandLineParser>


#define LOGFAULT_USE_QT_LOG 1
#include "funwithgrpc/logging.h"

using namespace std;

int main(int argc, char* argv[]) {

    try {
        locale loc("");
    } catch (const std::exception&) {
        cout << "Locales in Linux are fundamentally broken. Never worked. Never will. Overriding the current mess with LC_ALL=C" << endl;
        setenv("LC_ALL", "C", 1);
    }

    string_view log_level_qt =
#ifndef NDEBUG
        "trace";
#else
        "info";
#endif

    const auto appname = filesystem::path(argv[0]).stem().string();

    QGuiApplication app(argc, argv);

    QGuiApplication::setOrganizationName("TheLastViking");
    QGuiApplication::setApplicationName("QT gRPC Example");
    QGuiApplication::setApplicationVersion(VERSION);

    QCommandLineParser parser;
    parser.setApplicationDescription("Personal organizer");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"L", "log-level"}, "Set the log level for the log-file to one of: off, debug, trace, info",
                      "log-level", "info"});
    parser.addOption({{"C", "log-level-console"}, "Set the log level to the console to one of: off, debug, trace, info",
                      "log-level-console", "info"});
    parser.addOption({"log-file", "Path to the log file", "log-file"});
    parser.process(app);

    if (parser.isSet("log-level-console")) {
        log_level_qt = parser.value("log-level-console").toStdString();
    }

    if (auto level = toLogLevel(log_level_qt)) {
        logfault::LogManager::Instance().AddHandler(
            make_unique<logfault::QtHandler>(*level));
    }

    if (parser.isSet("log-file")) {
        if (auto path = parser.value("log-file").toStdString(); !path.empty()) {
            if (const auto level = toLogLevel(parser.value("log-level").toStdString()); level.has_value()) {
                logfault::LogManager::Instance().AddHandler(make_unique<logfault::StreamHandler>(path,*level, true));
            }
        }
    }

    LOG_INFO << appname << " starting up.";


    QQmlApplicationEngine engine;
    engine.loadFromModule("appExample", "Main");
    if (engine.rootObjects().isEmpty()) {
        LOG_ERROR << "Failed to initialize engine!";
        return -1;
    }

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() {
            LOG_ERROR << "Object creation failed!";
            QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);

    auto ret = app.exec();

    LOG_INFO << appname << " done! ";

    return ret;
} // main
