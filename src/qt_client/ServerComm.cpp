
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
    auto channelOptions = QGrpcChannelOptions{};

    client_.attachChannel(std::make_shared<QGrpcHttp2Channel>(
        QUrl(serverAddress, QUrl::StrictMode),
        channelOptions));
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
    }, [this](const std::optional<routeguide::Feature>& feature) {
        // Handle the result
        if (feature) {
            LOG_DEBUG << "Got Feature!";
            setStatus("Got Feature: " + feature->name());
        } else {
            LOG_DEBUG << "Failed to get Feature!";
            setStatus("Failed to get Feature");
        }
    });
}

void ServerComm::listFeatures()
{
    // Update the status in the UI.
    setStatus("...\n");

    // Prepare some data to send to the server.
    routeguide::Rectangle rect;
    rect.hi().setLatitude(1);
    rect.hi().setLatitude(2);
    rect.lo().setLatitude(3);
    rect.lo().setLatitude(4);

    // The stream is owned by client_.
    auto stream = client_.ListFeatures(rect);

    // Subscribe for incoming messages.
    connect(stream.get(), &QGrpcServerStream::messageReceived, [this, stream=stream.get()] {
        LOG_DEBUG << "Got message signal";
        if (stream->isFinished()) {
            LOG_DEBUG << "Stream finished";
            emit streamFinished();
            return;
        }

        if (const auto msg = stream->read<routeguide::Feature>()) {
            emit receivedMessage("Got feature: " + msg->name());
            setStatus(status_ + "Got feature: " + msg->name() + "\n");
        }
    });

    // Subscribe for the stream finished signal.
    connect (stream.get(), &QGrpcServerStream::finished, [this] {
        LOG_DEBUG << "Stream finished signal.";
        emit streamFinished();
    });
}

void ServerComm::recordRoute()
{
    // The stream is owned by client_.
    auto stream = client_.RecordRoute(routeguide::Point());
    recordRouteStream_ = stream;

    setStatus("Send messages...\n");

    connect(stream.get(), &QGrpcClientStream::finished, [this, stream=stream.get()] {
        LOG_DEBUG << "Stream finished signal.";

        if (auto msg = stream->read<routeguide::RouteSummary>()) {
            setStatus(status_ + "Finished trip with " + QString::number(msg->pointCount()) + " points\n");
        }
    });
}

void ServerComm::sendRouteUpdate()
{
    if (recordRouteStream_) {
        routeguide::Point point;
        point.setLatitude(1);
        point.setLongitude(2);
        recordRouteStream_->writeMessage(point);
        setStatus(status_ + "Sent one route update\n");
    } else {
        setStatus("ERROR: The RecordRoute stream has gone!");
        LOG_ERROR << "ERROR: The RecordRoute stream has gone!";
    }
}

void ServerComm::finishRecordRoute()
{
    if (recordRouteStream_) {
        recordRouteStream_->writesDone();
        setStatus(status_ + "Finished sending route updates\n");
    } else {
        setStatus("ERROR: The RecordRoute stream has gone!");
        LOG_ERROR << "ERROR: The RecordRoute stream has gone!";
    }
}

void ServerComm::routeChat()
{
    routeChatStream_ = client_.RouteChat(routeguide::RouteNote());

    connect(routeChatStream_.get(), &QGrpcBidirStream::messageReceived, [this, stream=routeChatStream_.get()] {
        if (const auto msg = stream->read<routeguide::RouteNote>()) {
            emit receivedMessage("Got chat message: " + msg->message());
            setStatus(status_ + "Got chat message: " + msg->message() + "\n");
        }
    });

    connect(routeChatStream_.get(), &QGrpcBidirStream::finished, [this] {
        LOG_DEBUG << "Stream finished signal.";
        emit streamFinished();
    });
}

void ServerComm::sendChatMessage(const QString& message)
{
    if (routeChatStream_) {
        routeguide::RouteNote note;
        note.setMessage(message);
        routeChatStream_->writeMessage(note);
        setStatus(status_ + "Sent one chat message\n");
    } else {
        setStatus("ERROR: The RouteChat stream has gone!");
        LOG_ERROR << "ERROR: The RouteChat stream has gone!";
    }
}

void ServerComm::finishRouteChat()
{
    if (routeChatStream_) {
        routeChatStream_->writesDone();
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
    setStatus(QString{"Error: Call to gRPC server failed: "} + status.message());
    setReady(false);
}

