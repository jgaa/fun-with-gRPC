#pragma once

#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>


#include "route_guide.grpc.pb.h"
#include "funwithgrpc/logging.h"
#include "funwithgrpc/Config.h"

/*!
 * \brief The SimpleReqRespCallbackSvc class
 *
 * This class implements just the basic unary RPC operation `GetFeature()`.
 */

class CallbackSvc {
public:

    template <typename T>
    class ReqBase {
    public:
        ReqBase() {
            LOG_TRACE << "Creating instance for request# " << client_id_;
        }

        void done() {
            // Ugly, ugly, ugly
            LOG_TRACE << "If the program crash now, it was a bad idea to delete this ;)  #"
                      << client_id_ << " at address " << this;
            delete static_cast<T *>(this);
        }

        static size_t getNewClientId() {
            static size_t id = 0;
            return ++id;
        }

        std::string me() const {
            return boost::typeindex::type_id_runtime(static_cast<const T&>(*this)).pretty_name()
                   + " #"
                   + std::to_string(client_id_);
        }

    protected:
        const size_t client_id_ = getNewClientId();
    };

    template <typename T, typename... Args>
    static auto createNew(CallbackSvc& parent, Args... args) {

        try {
            return new T(parent, args...);
            // If we got here, the instance should be fine, so let it handle itself.
        } catch(const std::exception& ex) {
            LOG_ERROR << "Got exception while creating a new instance. "
                      << "This ends the jurney for this instance of me. "
                      << " Error: " << ex.what();
        }

        abort();
    }

    class CallbackServiceImpl : public ::routeguide::RouteGuide::CallbackService {
    public:
        CallbackServiceImpl(CallbackSvc& owner)
            : owner_{owner} {}

        // RPC's
        grpc::ServerUnaryReactor *GetFeature(grpc::CallbackServerContext *ctx,
                                             const routeguide::Point *req,
                                             routeguide::Feature *resp) override {
            assert(ctx);
            assert(req);
            assert(resp);

            LOG_TRACE << "Dealing with one GetFeature() RPC. latitude="
                      << req->latitude()
                      << ", longitude=" << req->longitude()
                      << ", peer=" << ctx->peer();

            // Give a nice response
            resp->set_name("whatever");

            // We could have implemented our own reactor, but this is the recommended
            // way to do it in unary methods.
            auto* reactor = ctx->DefaultReactor();
            reactor->Finish(grpc::Status::OK);
            return reactor;
        }

        ::grpc::ServerWriteReactor< ::routeguide::Feature>* ListFeatures(
            ::grpc::CallbackServerContext* ctx, const ::routeguide::Rectangle* req) override {

            // We're using a class to implement the work-flow required to answer the request
            // We can only send one message at the time and must wait for callbacks to continue.

            // Yes - it's ugly. It's the best the gRPC team have been able to come up with :/
            class ServerWriteReactorImpl
                : public ReqBase<ServerWriteReactorImpl>
                , public ::grpc::ServerWriteReactor< ::routeguide::Feature> {
            public:
                ServerWriteReactorImpl(CallbackSvc& owner)
                    : owner_{owner} {
                    reply();
                }

                // Overrides for the ServerWriteReactor interface
                void OnDone() override {
                    done();
                }

                void OnWriteDone(bool ok) override {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << "The write-operation failed.";

                        // We still need to call Finish!
                        Finish(grpc::Status::OK);
                        return;
                    }

                    reply();
                }

            private:
                void reply() {
                    // Reply with the number of messages in config
                    if (++replies_ > owner_.config().num_stream_messages) {
                        reply_.Clear();

                        // Since it's a stream, it make sense to return different data for each message.
                        reply_.set_name(std::string{"stream-reply #"} + std::to_string(replies_));

                        StartWrite(&reply_);
                        return;
                    }

                    // Didn't write anything, all is done.
                    Finish(grpc::Status::OK);
                }

                CallbackSvc& owner_;
                size_t replies_ = 0;
                ::routeguide::Feature reply_;
            };

            return createNew<ServerWriteReactorImpl>(owner_);
        };

        ::grpc::ServerReadReactor< ::routeguide::Point>* RecordRoute(
            ::grpc::CallbackServerContext* ctx, ::routeguide::RouteSummary* reply) override {

            class ServerReadReactorImpl
                : public ReqBase<ServerReadReactorImpl>
                , public grpc::ServerReadReactor<::routeguide::Point> {
            public:
                ServerReadReactorImpl(CallbackSvc& owner, ::routeguide::RouteSummary* reply)
                    : owner_{owner}, reply_{reply} {
                    assert(reply_);
                    StartRead(&req_);
                }

                void OnDone() override {
                    done();
                }

                void OnReadDone(bool ok) override {
                    if (ok) {
                        // This is where we have read a message from the request.
                        // If this was code for a framework, this is where we would have called
                        // the `onRpcRequestRecordRouteGotMessage()` method, or unblocked the next statement
                        // in a co-routine awaiting the next state-change.
                        //
                        // In our case, let's just log it.
                        LOG_TRACE << "Got message: longitude=" << req_.longitude()
                                  << ", latitude=" << req_.latitude();

                        // Reset the req_ message. This is cheaper than allocating a new one for each read.
                        req_.Clear();
                        StartRead(&req_);
                    } else {
                        LOG_TRACE << "The read-operation failed. It's probably not an error :)";

                        // This is where we have received the request, with all it's parts,
                        // and may formulate another answer.
                        // If this was code for a framework, this is where we would have called
                        // the `onRpcRequestRecordRouteDone()` method, or unblocked the next statement
                        // in a co-routine awaiting the next state-change.
                        //
                        // In our case, let's just return something.

                        reply_->set_distance(100);
                        reply_->set_distance(300);

                        // Here we can set the reply (in the buffer we got from gRPC) and call
                        // Finish in one go. We don't have to wait for a callback to acknowledge
                        // the write operation.

                        Finish(grpc::Status::OK);
                    }
                }

            private:
                CallbackSvc& owner_;
                ::routeguide::Point req_;
                ::routeguide::RouteSummary *reply_ = {};
            };

            return createNew<ServerReadReactorImpl>(owner_, reply);
        };

        ::grpc::ServerBidiReactor< ::routeguide::RouteNote, ::routeguide::RouteNote>* RouteChat(
            ::grpc::CallbackServerContext* ctx) override {

            class ServerBidiReactorImpl
                : public ReqBase<ServerBidiReactorImpl>
                , public grpc::ServerBidiReactor<::routeguide::RouteNote, ::routeguide::RouteNote> {
            public:
                ServerBidiReactorImpl(CallbackSvc& owner)
                    : owner_{owner} {

                    /* There are multiple ways to handle the message-flow in a bidirectional stream.
                     *
                     * One party can send the first message, and the other party can respond with a message,
                     * until the one or both parties gets bored.
                     *
                     * Both parties can wait for some event to occur, and send a message when appropriate.
                     *
                     * One party can send occasional updates (for example location data) and the other
                     * party can respond with one or more messages when appropriate.
                     *
                     * Both parties can start sending messages as soon as the connection is made.
                     * That's what we are doing (or at least preparing for) in this example.
                     */

                    read(true);   // Initiate the read for the first incoming message
                    write(true);  // Initiate the first write operation.
                }

                void OnDone() override {
                    done();
                }

                void OnReadDone(bool ok) override {
                    if (!ok) {
                        LOG_TRACE << me() << "- The read-operation failed. It's probably not an error :)";
                        done_reading_ = true;
                        return finishIfDone();

                    }

                    read(false);
                }
                void OnWriteDone(bool ok) override {
                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        LOG_WARN << "The write-operation failed.";

                        // When ok is false here, we will not be able to write
                        // anything on this stream.
                        done_writing_ = true;
                        return finishIfDone();
                    }

                    write(false);
                }

            private:
                void read(const bool first) {
                    if (!first) {
                        // This is where we have read a message from the stream.
                        // If this was code for a framework, this is where we would have called
                        // the `onRpcRequestRouteChatGotMessage()` method, or unblocked the next statement
                        // in a co-routine awaiting the next state-change.
                        //
                        // In our case, let's just log it.

                        LOG_TRACE << "Incoming message: " << req_.message();
                        req_.Clear();
                    }

                    // Start new read
                    StartRead(&req_);
                }

                void write(const bool first) {
                    if (!first) {
                        reply_.Clear();
                    }

                    if (++replies_ > owner_.config().num_stream_messages) {
                        done_writing_ = true;

                        LOG_TRACE << me() << " - We are done writing to the stream.";
                        return finishIfDone();
                    }

                    // This is where we are ready to write a new message.
                    // If this was code for a framework, this is where we would have called
                    // the `onRpcRequestRouteChatReadytoSendNewMessage()` method, or unblocked
                    // the next statement in a co-routine awaiting the next state-change.

                    reply_.set_message(std::string{"Server Message #"} + std::to_string(replies_));

                    // Start new write
                    StartWrite(&reply_);
                }

                void finishIfDone() {
                    if (!sent_finish_ && done_reading_ && done_writing_) {
                        LOG_TRACE << me() << " - We are done reading and writing. Sending finish!";
                        Finish(grpc::Status::OK);
                        sent_finish_ = true;
                        return;
                    }
                }

                CallbackSvc& owner_;
                ::routeguide::RouteNote req_;
                ::routeguide::RouteNote reply_;
                size_t replies_ = 0;
                bool done_reading_ = false;
                bool done_writing_ = false;
                bool sent_finish_ = false;
            };


            auto instance = createNew<ServerBidiReactorImpl>(owner_);
            LOG_TRACE << instance->me()
                      << " - Starting new bidirectional stream conversation with "
                      << ctx->peer();
            return instance;
        }

    private:
        CallbackSvc& owner_;
    };

    void init() {
        grpc::ServerBuilder builder;

        // Tell gRPC what TCP address/port to listen to and how to handle TLS.
        // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.
        builder.AddListeningPort(config_.address, grpc::InsecureServerCredentials());

        // Feed gRPC our implementation of the RPC's
        service_ = std::make_unique<CallbackServiceImpl>(*this);
        builder.RegisterService(service_.get());

        // Get a queue for our async events
        cq_ = builder.AddCompletionQueue();

        // Finally assemble the server.
        server_ = builder.BuildAndStart();
        LOG_INFO
            // Fancy way to print the class-name.
            // Useful when I copy/paste this code around ;)
            << boost::typeindex::type_id_runtime(*this).pretty_name()

            // The useful information
            << " listening on " << config_.address;
    }

    CallbackSvc(Config& config)
        : config_{config} {}

    void start() {
        init();

        // Prepare for the first request.
        //OneRequest::createNew(service_, *cq_);
    }

    void stop() {
        LOG_INFO << "Shutting down "
                 << boost::typeindex::type_id_runtime(*this).pretty_name();
        server_->Shutdown();
        server_->Wait();
    }

    const Config& config() const noexcept {
        return config_;
    }

private:
    // An instance of our service, compiled from code generated by protoc
    std::unique_ptr<CallbackServiceImpl> service_;

    // This is the Queue. It's shared for all the requests.
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;

    // A gRPC server object
    std::unique_ptr<grpc::Server> server_;

    const Config& config_;
};
