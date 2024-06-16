#pragma once

#include <atomic>

#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>


#include "route_guide.grpc.pb.h"
#include "funwithgrpc/logging.h"
#include "funwithgrpc/Config.h"

/*!
 * \brief The CallbackSvc class
 *
 * This class implements all the methods using the gRPC callback
 * interface.
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

    /*! RPC implementation
     *
     *  This class overrides our RPC methods from the code
     *  generatoed by rpcgen. This is where we receive the RPC events from gRPC.
     */
    class CallbackServiceImpl : public ::routeguide::RouteGuide::CallbackService {
    public:
        CallbackServiceImpl(CallbackSvc& owner)
            : owner_{owner} {}

        /*! RPC callback event for GetFeature
         */
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

            // Give a nice, thoughtful response
            resp->set_name("whatever");

            // We could have implemented our own reactor, but this is the recommended
            // way to do it in unary methods.
            auto* reactor = ctx->DefaultReactor();
            reactor->Finish(grpc::Status::OK);
            return reactor;
        }

        /*! RPC callback event for ListFeatures
         */
        ::grpc::ServerWriteReactor< ::routeguide::Feature>* ListFeatures(
            ::grpc::CallbackServerContext* ctx, const ::routeguide::Rectangle* req) override {

            // Now, we need to keep the state and buffers required to handle the stream
            // until the request is completed. We also need to return from this
            // method immediately.
            //
            // We solve these conflicting requirements by implementing a class that overrides
            // the async gRPC stream interface `ServerWriteReactor`, and then adding our
            // state and buffers here.
            //
            // This class will deal with the request. The method we are in will just
            // allocate an instance of our class on the heap and return.
            //
            // Yes - it's ugly. It's the best the gRPC team have been able to come up with :/
            class ServerWriteReactorImpl
                // Some shared code we need for convenience for all the RPC Request classes
                : public ReqBase<ServerWriteReactorImpl>
                // The interface to the gRPC async stream for this request.
                , public ::grpc::ServerWriteReactor< ::routeguide::Feature> {
            public:
                ServerWriteReactorImpl(CallbackSvc& owner)
                    : owner_{owner} {

                    // Start replying with the first message on the stream
                    reply();
                }

                /*! Callback event when the RPC is completed */
                void OnDone() override {
                    done();
                }

                /*! Callback event when a write operation is complete */
                void OnWriteDone(bool ok) override {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << "The write-operation failed.";

                        // We still need to call Finish or the request will remain stuck!
                        Finish({grpc::StatusCode::UNKNOWN, "stream write failed"});
                        return;
                    }

                    reply();
                }

            private:
                void reply() {
                    // Reply with the number of messages in config
                    if (++replies_ < owner_.config().num_stream_messages) {
                        reply_.Clear();

                        // Since it's a stream, it make sense to return different data for each message.
                        reply_.set_name(std::string{"stream-reply #"} + std::to_string(replies_));

                        return StartWrite(&reply_);
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

        /*! RPC callback event for RecordRoute
         */
        ::grpc::ServerReadReactor< ::routeguide::Point>* RecordRoute(
            ::grpc::CallbackServerContext* ctx, ::routeguide::RouteSummary* reply) override {

            class ServerReadReactorImpl
                // Some shared code we need for convenience for all the RPC Request class
                : public ReqBase<ServerReadReactorImpl>
                // The async gRPC stream interface for this RPC
                , public grpc::ServerReadReactor<::routeguide::Point> {
            public:
                ServerReadReactorImpl(CallbackSvc& owner, ::routeguide::RouteSummary* reply)
                    : owner_{owner}, reply_{reply} {
                    assert(reply_);

                    // Initiate the first read operation
                    StartRead(&req_);
                }

                 /*! Callback event when the RPC is complete */
                void OnDone() override {
                    done();
                }

                /*! Callback event when a read operation is complete */
                void OnReadDone(bool ok) override {
                    if (ok) {
                        // We have read a message from the request.

                        LOG_TRACE << "Got message: longitude=" << req_.longitude()
                                  << ", latitude=" << req_.latitude();

                        req_.Clear();

                        // Initiate the next async read
                        return StartRead(&req_);
                    }

                    LOG_TRACE << "The read-operation failed. It's probably not an error :)";

                    // Let's compose an exiting reply to the client.
                    reply_->set_distance(100);
                    reply_->set_distance(300);

                    // Note that we set the reply (in the buffer we got from gRPC) and call
                    // Finish in one go. We don't have to wait for a callback to acknowledge
                    // the write operation.
                    Finish(grpc::Status::OK);
                    // When the client has received the last bits from us in regard of this
                    // RPC, `OnDone()` will be the final event we receive.
                }

            private:
                CallbackSvc& owner_;

                // Our buffer for each of the outgoing messages on the stream
                ::routeguide::Point req_;

                // Note that for this RPC type, gRPC owns the reply-buffer.
                // It's a bit inconsistent and confusing, as the interfaces mostly takes
                // pointers, but usually our implementation owns the buffers.
                ::routeguide::RouteSummary *reply_ = {};
            };

            // This is all our method actually does. It just creates an instance
            // of the implementation class to deal with the request.
            return createNew<ServerReadReactorImpl>(owner_, reply);
        };

        /*! RPC callback event for RouteChat
         */
        ::grpc::ServerBidiReactor< ::routeguide::RouteNote, ::routeguide::RouteNote>*
            RouteChat(::grpc::CallbackServerContext* ctx) override {

            class ServerBidiReactorImpl
                // Some shared code we need for convenience for all the RPC Request classes
                : public ReqBase<ServerBidiReactorImpl>
                // The async gRPC stream interface for this RPC
                , public grpc::ServerBidiReactor<::routeguide::RouteNote, ::routeguide::RouteNote> {
            public:
                ServerBidiReactorImpl(CallbackSvc& owner)
                    : owner_{owner} {

                    /* There are multiple ways to handle the message-flow in a bidirectional stream.
                     *
                     * One party can send the first message, and the other party can respond with a message,
                     * until one or both parties gets bored.
                     *
                     * Both parties can wait for some event to occur, and send a message when appropriate.
                     *
                     * One party can send occasional updates (for example location data) and the other
                     * party can respond with one or more messages when appropriate.
                     *
                     * Both parties can start sending messages as soon as the connection is made.
                     * That's what we are doing (or at least preparing for) in this example.
                     */

                    read();   // Initiate the read for the first incoming message
                    write();  // Initiate the first write operation.
                }

                /*! Callback event when the RPC is complete */
                void OnDone() override {
                    done();
                }

                /*! Callback event when a read operation is complete */
                void OnReadDone(bool ok) override {
                    if (!ok) {
                        LOG_TRACE << me() << "- The read-operation failed. It's probably not an error :)";
                        done_reading_ = true;
                        return finishIfDone();
                    }

                    LOG_TRACE << "Incoming message: " << req_.message();
                    read();
                }

                /*! Callback event when a write operation is complete */
                void OnWriteDone(bool ok) override {
                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        LOG_WARN << "The write-operation failed.";

                        // When ok is false here, we will not be able to write
                        // anything on this stream.
                        done_writing_ = true;

                        // This RPC call did not end well
                        status_ = {grpc::StatusCode::UNKNOWN, "write failed"};

                        return finishIfDone();
                    }

                    write();
                }

            private:
                void read() {
                    // Start a new read
                    req_.Clear();
                    StartRead(&req_);
                }

                void write() {
                    if (++replies_ > owner_.config().num_stream_messages) {
                        done_writing_ = true;

                        LOG_TRACE << me() << " - We are done writing to the stream.";
                        return finishIfDone();
                    }

                    // Prepare a new message to the stream
                    reply_.Clear();

                    // Shout something nice to the other side
                    reply_.set_message(std::string{"Server Message #"} + std::to_string(replies_));

                    // Start new write on the stream
                    StartWrite(&reply_);
                }

                void finishIfDone() {
                    if (!sent_finish_ && done_reading_ && done_writing_) {
                        LOG_TRACE << me() << " - We are done reading and writing. Sending finish!";
                        Finish(status_);
                        sent_finish_ = true;
                        return;
                    }
                }

                CallbackSvc& owner_;
                ::routeguide::RouteNote req_;
                ::routeguide::RouteNote reply_;
                grpc::Status status_;
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
    }; // class CallbackServiceImpl

    CallbackSvc(Config& config)
        : config_{config} {}

    void start() {
        grpc::ServerBuilder builder;

        // Tell gRPC what TCP address/port to listen to and how to handle TLS.
        // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.
        builder.AddListeningPort(config_.address, grpc::InsecureServerCredentials());

        // Feed gRPC our implementation of the RPC's
        service_ = std::make_unique<CallbackServiceImpl>(*this);
        builder.RegisterService(service_.get());

        // Finally assemble the server.
        server_ = builder.BuildAndStart();
        LOG_INFO
            // Fancy way to print the class-name.
            // Useful when I copy/paste this code around ;)
            << boost::typeindex::type_id_runtime(*this).pretty_name()

            // The useful information
            << " listening on " << config_.address;
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
    const Config& config_;

    // Thread-safe method to get a unique client-id for a new RPC.
    static size_t getNewClientId() {
        static std::atomic_size_t id{0};
        return ++id;
    }

    // An instance of our service, compiled from code generated by protoc
    std::unique_ptr<CallbackServiceImpl> service_;

    // A gRPC server object
    std::unique_ptr<grpc::Server> server_;
};
