
#include <QGrpcChannelOptions>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>

#include "ServerComm.h"



ServerComm::ServerComm(QObject *parent)
    : QObject(parent)
{
    connect(&client_, &routeguide::RouteGuide::Client::errorOccurred,
            this, &ServerComm::errorOccurred);

}

void ServerComm::start(const QString &serverAddress)
{
    auto channelOptions = QGrpcChannelOptions{QUrl(serverAddress,
                                                   QUrl::StrictMode)};

    client_.attachChannel(std::make_shared<QGrpcHttp2Channel>(channelOptions));
    LOG_INFO << "Using server at " << serverAddress;

    setReady(true);
}

void ServerComm::getFeature()
{
    routeguide::Point point;
    point.setLatitude(1);
    point.setLongitude(2);

    callRpc<routeguide::Feature>([&] {
        LOG_DEBUG << "Calling GetFeature...";
        return client_.GetFeature(point); // Call the gRPC method
    }, [this](const routeguide::Feature& feature) {
        // Handle the result
        LOG_DEBUG << "Got Feature!";
        setStatus("Got Feature: " + feature.name());
    });
}

void ServerComm::setStatus(QString status)
{
    if (status != status_) {
        status_ = std::move(status);
        emit statusChanged();
    }
}

void ServerComm::setReady(bool ready)
{
    if (ready != ready_) {
        ready_ = ready;
        emit readyChanged();
    }
}

void ServerComm::errorOccurred(const QGrpcStatus &status)
{
    LOG_ERROR << "errorOccurred: Call to gRPC server failed: " << status.message();
}

