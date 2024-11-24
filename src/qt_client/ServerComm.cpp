
#include <QGrpcChannelOptions>
#include <QGrpcHttp2Channel>
#include <QtConcurrent/QtConcurrent>

#include "ServerComm.h"

ServerComm::ServerComm(QObject *parent)
    : QObject(parent)
{
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

    auto point = [](int latitude, int longitude) {
        routeguide::Point p;
        p.setLatitude(latitude);
        p.setLongitude(longitude);
        return p;
    };

    // Prepare some data to send to the server.
    routeguide::Rectangle rect;

    rect.setHi(point(1, 2));
    rect.setLo(point(3, 4));

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
    recordRouteStream_ = client_.RecordRoute(routeguide::Point{});

    setStatus("Send messages...\n");

    connect(recordRouteStream_.get(), &QGrpcClientStream::finished, [this] {
        LOG_DEBUG << "Stream finished signal.";

        if (auto msg = recordRouteStream_->read<routeguide::RouteSummary>()) {
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

    connect(routeChatStream_.get(), &QGrpcBidiStream::messageReceived, [this, stream=routeChatStream_.get()] {
        if (const auto msg = stream->read<routeguide::RouteNote>()) {
            emit receivedMessage("Got chat message: " + msg->message());
            setStatus(status_ + "Got chat message: " + msg->message() + "\n");
        }
    });

    connect(routeChatStream_.get(), &QGrpcBidiStream::finished, [this] {
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

QString ServerComm::toString(const QGrpcStatus& status) {
    static const auto errors = std::to_array<QString>({
        tr("OK"),
        tr("CANCELLED"),
        tr("UNKNOWN"),
        tr("INVALID_ARGUMENT"),
        tr("DEADLINE_EXCEEDED"),
        tr("NOT_FOUND"),
        tr("ALREADY_EXISTS"),
        tr("PERMISSION_DENIED"),
        tr("RESOURCE_EXHAUSTED"),
        tr("FAILED_PRECONDITION"),
        tr("ABORTED"),
        tr("OUT_OF_RANGE"),
        tr("UNIMPLEMENTED"),
        tr("INTERNAL"),
        tr("UNAVAILABLE"),
        tr("DATA_LOSS"),
        tr("UNAUTHENTICATED"),
    });

    const auto ix = static_cast<size_t>(status.code());
    if (ix < errors.size()) {
        return errors[ix] + ": " + status.message();
    }

    return tr("UNKNOWN (code=%1): %2").arg(ix).arg(status.message());
}
