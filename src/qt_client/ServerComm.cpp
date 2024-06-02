
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
    setStatus("Ready");
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

void ServerComm::listFeatures()
{
    setStatus("...\n");

    routeguide::Rectangle rect;
    rect.hi().setLatitude(1);
    rect.hi().setLatitude(2);
    rect.lo().setLatitude(3);
    rect.lo().setLatitude(4);

    // The stream is owned by client_.
    auto stream = client_.streamListFeatures(rect);

    connect(stream.get(), &QGrpcServerStream::messageReceived, [this, stream=stream.get()] {
        LOG_DEBUG << "Got message signal";
        if (stream->isFinished()) {
            LOG_DEBUG << "Stream finished";
            emit streamFinished();
            return;
        }

        const auto msg = stream->read<routeguide::Feature>();
        emit receivedMessage("Got feature: " + msg.name());
        setStatus(status_ + "Got feature: " + msg.name() + "\n");
    });

    connect (stream.get(), &QGrpcServerStream::finished, [this] {
        LOG_DEBUG << "Stream finished signal.";
        emit streamFinished();
    });

    connect (stream.get(), &QGrpcServerStream::errorOccurred, [this] (const QGrpcStatus &status){
        LOG_DEBUG << "gRPC Stream Error: " << status.message();
        emit streamFinished();
    });
}

void ServerComm::recordRoute()
{
    // The stream is owned by client_.
    auto stream = client_.streamRecordRoute(routeguide::Point());
    recordRouteStream_ = stream;

    setStatus("Send messages...\n");

    connect (stream.get(), &QGrpcServerStream::errorOccurred, [this] (const QGrpcStatus &status){
        LOG_DEBUG << "gRPC Stream Error: " << status.message();
        emit streamFinished();
    });

    connect(stream.get(), &QGrpcClientStream::finished, [this, stream=stream.get()] {
        LOG_DEBUG << "Stream finished signal.";
        auto msg = stream->read<routeguide::RouteSummary>();
        setStatus(status_ + "Finished trip with " + QString::number(msg.pointCount()) + " points\n");

    });
}

void ServerComm::sendRouteUpdate()
{
    if (auto stream = recordRouteStream_) {
        routeguide::Point point;
        point.setLatitude(1);
        point.setLongitude(2);
        stream->sendMessage(point);
        setStatus(status_ + "Sent one route update\n");
    } else {
        setStatus("ERROR: The RecordRoute stream has gone!");
        LOG_ERROR << "ERROR: The RecordRoute stream has gone!";
    }
}

void ServerComm::finishRecordRoute()
{
    if (auto stream = recordRouteStream_) {

        // I have not found a proper way to Finish the stream.
        // This is the closest I got...
        stream->cancel();
        setStatus(status_ + "Finished sending route updates\n");
    } else {
        setStatus("ERROR: The RecordRoute stream has gone!");
        LOG_ERROR << "ERROR: The RecordRoute stream has gone!";
    }
}

void ServerComm::routeChat()
{
    routeChatStream_ = client_.streamRouteChat(routeguide::RouteNote());

    connect(routeChatStream_.get(), &QGrpcBidirStream::messageReceived, [this, stream=routeChatStream_.get()] {
        const auto msg = stream->read<routeguide::RouteNote>();
        emit receivedMessage("Got chat message: " + msg.message());
        setStatus(status_ + "Got chat message: " + msg.message() + "\n");
    });

    connect(routeChatStream_.get(), &QGrpcBidirStream::finished, [this] {
        LOG_DEBUG << "Stream finished signal.";
        emit streamFinished();
    });

    connect(routeChatStream_.get(), &QGrpcBidirStream::errorOccurred, [this] (const QGrpcStatus &status) {
        LOG_DEBUG << "gRPC Stream Error: " << status.message();
        emit streamFinished();
    });
}

void ServerComm::sendChatMessage(const QString& message)
{
    if (auto stream = routeChatStream_) {
        routeguide::RouteNote note;
        note.setMessage(message);
        stream->sendMessage(note);
        setStatus(status_ + "Sent one chat message\n");
    } else {
        setStatus("ERROR: The RouteChat stream has gone!");
        LOG_ERROR << "ERROR: The RouteChat stream has gone!";
    }
}

void ServerComm::finishRouteChat()
{
    // ???
    if (auto stream = routeChatStream_) {
        stream->cancel();
        setStatus(status_ + "Finished sending chat messages\n");
    } else {
        setStatus("ERROR: The RouteChat stream has gone!");
        LOG_ERROR << "ERROR: The RouteChat stream has gone!";
    }
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

