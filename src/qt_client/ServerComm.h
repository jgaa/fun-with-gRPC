#pragma once

#include <queue>
#include <QObject>
#include <QQmlEngine>
#include <QProtobufMessage>
#include <QGrpcClientStream>

#include "route_guide_client.grpc.qpb.h"

#define LOGFAULT_USE_QT_LOG 1
#include "funwithgrpc/logging.h"

template <typename T, typename... Y>
concept ValidFunctor = std::invocable<T&, Y...>;

template <typename T>
concept ProtoMessage = std::is_base_of_v<QProtobufMessage, T>;

class ServerComm : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    ServerComm(QObject *parent = {});

    /*! Start the gRPC client.
     * This method is called from QML.
     *
     * We can call it again to change the server address or for example
     * if the server restarted.
     */
    Q_INVOKABLE void start(const QString& serverAddress);

    /*! Call's GetFeature on the server */
    Q_INVOKABLE void getFeature();
    Q_INVOKABLE void listFeatures();
    Q_INVOKABLE void recordRoute();
    Q_INVOKABLE void sendRouteUpdate();
    Q_INVOKABLE void finishRecordRoute();

    Q_INVOKABLE void routeChat();
    Q_INVOKABLE void sendChatMessage(const QString& message);
    Q_INVOKABLE void finishRouteChat();

    // Simple template to hide the complexity of calling a normal gRPC method.
    // It takes a method to call with its arguments and a functor to be called when the result is ready.
    template <typename respT, typename callT, typename doneT, typename ...Args>
    void callRpc(callT&& call, doneT && done, Args... args) {
        auto exec = [this, call=std::move(call), done=std::move(done), args...]() {
            auto rpc_method = call(args...);
            rpc_method->subscribe(this, [this, rpc_method, done=std::move(done)] () {
                    std::optional<respT> rval = rpc_method-> template read<respT>();
                    if constexpr (ValidFunctor<doneT, respT>) {
                        done(rval);
                    } else {
                        // The done functor must either be valid callable functor, or 'false'
                        static_assert(std::is_same_v<doneT, bool>);
                        assert(!done);
                    }
                },
                [this](QGrpcStatus status) {
                    LOG_ERROR << "Comm error: " << status.message();
                });
        };

        exec();
    }

signals:
    void statusChanged();
    void readyChanged();
    void receivedMessage(const QString& message);
    void streamFinished();

private:
    QString status() const noexcept {
        return status_;
    }

    bool ready() const noexcept {
        return ready_;
    }

    void setStatus(QString status);
    void setReady(bool ready);

    routeguide::RouteGuide::Client client_;
    void errorOccurred(const QGrpcStatus &status);
    bool ready_{false};
    QString status_ = "Idle. Please press a button.";
    std::shared_ptr<QGrpcClientStream> recordRouteStream_;
    std::shared_ptr<QGrpcBidirStream> routeChatStream_;
};
