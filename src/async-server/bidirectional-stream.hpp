
#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>

#include "funwithgrpc/BaseRequest.hpp"
#include "route_guide.grpc.pb.h"
#include "funwithgrpc/logging.h"
#include "funwithgrpc/Config.h"

class EverythingSvr
    : public EventLoopBase<ServerVars<::routeguide::RouteGuide>> {
public:


    class GetFeatureRequest : public RequestBase {
    public:

        GetFeatureRequest(EverythingSvr& owner)
            : RequestBase(owner) {

            // Register this instance with the event-queue and the service.
            // The first event received over the queue is that we have a request.
            owner_.grpc().service_.RequestGetFeature(&ctx_, &req_, &resp_, cq(), cq(),
                op_handle_.tag(Handle::Operation::CONNECT,
                [this, &owner](bool ok, Handle::Operation /* op */) {

                LOG_DEBUG << me(*this) << " - Processing a new connect from " << ctx_.peer();

                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        // Let's end it here.
                        LOG_WARN << "The request-operation failed. Assuming we are shutting down";
                        return;
                    }

                    // Before we do anything else, we must create a new instance of
                    // GetFeatureRequest, so the service can handle a new request from a client.
                    // Note that we instantiate with `owner`, not `owner_`, as `owner`
                    // has the complete typeinfo of `EverythingSvr`.
                    owner_.createNew<GetFeatureRequest>(owner);

                    // This is where we have the request, and may formulate an answer.
                    // If this was code for a framework, this is where we would have called
                    // the `onRpcRequestGetFeature()` method, or unblocked the next statement
                    // in a co-routine waiting for the next request.
                    //
                    // In our case, let's just return something.
                    reply_.set_name("whatever");
                    reply_.mutable_location()->CopyFrom(req_);

                    // Initiate our next async operation.
                    // That will complete when we have sent the reply, or replying failed.
                    resp_.Finish(reply_, ::grpc::Status::OK,
                        op_handle_.tag(Handle::Operation::FINISH,
                        [this](bool ok, Handle::Operation /* op */) {

                            if (!ok) [[unlikely]] {
                                LOG_WARN << "The finish-operation failed.";
                            }

                    }));// FINISH operation lambda
                })); // CONNECT operation lambda
        }

    private:
        Handle op_handle_{*this}; // We need only one handle for this operation.

        ::grpc::ServerContext ctx_;
        ::routeguide::Point req_;
        ::routeguide::Feature reply_;
        ::grpc::ServerAsyncResponseWriter<decltype(reply_)> resp_{&ctx_};
    };

    class ListFeaturesRequest : public RequestBase {
    public:

        ListFeaturesRequest(EverythingSvr& owner)
            : RequestBase(owner) {

            owner_.grpc().service_.RequestListFeatures(&ctx_, &req_, &resp_, cq(), cq(),
                op_handle_.tag(Handle::Operation::CONNECT,
                [this, &owner](bool ok, Handle::Operation /* op */) {

                    LOG_DEBUG << me(*this) << " - Processing a new connect from " << ctx_.peer();

                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        // Let's end it here.
                        LOG_WARN << "The request-operation failed. Assuming we are shutting down";
                        return;
                    }

                    // Before we do anything else, we must create a new instance
                    // so the service can handle a new request from a client.
                    owner_.createNew<ListFeaturesRequest>(owner);

                reply();
            }));
        }

    private:
        void reply() {
            if (++replies_ > owner_.config().num_stream_messages) {
                // We have reached the desired number of replies

                resp_.Finish(::grpc::Status::OK,
                    op_handle_.tag(Handle::Operation::FINISH,
                    [this](bool ok, Handle::Operation /* op */) {
                        if (!ok) [[unlikely]] {
                            // The operation failed.
                            LOG_WARN << "The finish-operation failed.";
                        }
                }));

                return;
            }

            // This is where we have the request, and may formulate another answer.
            // If this was code for a framework, this is where we would have called
            // the `onRpcRequestListFeaturesOnceAgain()` method, or unblocked the next statement
            // in a co-routine awaiting the next state-change.
            //
            // In our case, let's just return something.


            // Prepare the reply-object to be re-used.
            // This is usually cheaper than creating a new one for each write operation.
            reply_.Clear();

            // Since it's a stream, it make sense to return different data for each message.
            reply_.set_name(std::string{"stream-reply #"} + std::to_string(replies_));

            resp_.Write(reply_, op_handle_.tag(Handle::Operation::FINISH,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        LOG_WARN << "The reply-operation failed.";
                        return;
                    }

                    reply();
            }));
        }

        Handle op_handle_{*this}; // We need only one handle for this operation.
        size_t replies_ = 0;

        ::grpc::ServerContext ctx_;
        ::routeguide::Rectangle req_;
        ::routeguide::Feature reply_;
        ::grpc::ServerAsyncWriter<decltype(reply_)> resp_{&ctx_};
    };

    class RecordRouteRequest : public RequestBase {
    public:

        RecordRouteRequest(EverythingSvr& owner)
            : RequestBase(owner) {

            owner_.grpc().service_.RequestRecordRoute(&ctx_, &io_, cq(), cq(),
                op_handle_.tag(Handle::Operation::CONNECT,
                    [this, &owner](bool ok, Handle::Operation /* op */) {

                        LOG_DEBUG << me(*this) << " - Processing a new connect from " << ctx_.peer();

                        if (!ok) [[unlikely]] {
                            // The operation failed.
                            // Let's end it here.
                            LOG_WARN << "The request-operation failed. Assuming we are shutting down";
                            return;
                        }

                      // Before we do anything else, we must create a new instance
                      // so the service can handle a new request from a client.
                      owner_.createNew<RecordRouteRequest>(owner);

                      read(true);
                  }));
        }

    private:
        void read(const bool first) {

            if (!first) {
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
            }

            io_.Read(&req_,  op_handle_.tag(Handle::Operation::READ,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        // This is normal on an incoming stream, when there are no more messages.
                        // As far as I know, there is no way at this point to deduce if the false status is
                        // because the client is done sending messages, or because we encountered
                        // an error.
                        LOG_TRACE << "The read-operation failed. It's probably not an error :)";

                        // Initiate the finish operation

                        // This is where we have received the request, with all it's parts,
                        // and may formulate another answer.
                        // If this was code for a framework, this is where we would have called
                        // the `onRpcRequestRecordRouteDone()` method, or unblocked the next statement
                        // in a co-routine awaiting the next state-change.
                        //
                        // In our case, let's just return something.

                        reply_.set_distance(100);
                        reply_.set_distance(300);
                        io_.Finish(reply_, ::grpc::Status::OK, op_handle_.tag(
                            Handle::Operation::FINISH,
                            [this](bool ok, Handle::Operation /* op */) {

                             if (!ok) [[unlikely]] {
                                LOG_WARN << "The finish-operation failed.";
                            }

                            // We are done
                        }));
                        return;
                    } // ok != false

                    read(false);
            }));

        }

        Handle op_handle_{*this}; // We need only one handle for this operation.

        ::grpc::ServerContext ctx_;
        ::routeguide::Point req_;
        ::routeguide::RouteSummary reply_;
        ::grpc::ServerAsyncReader< decltype(reply_), decltype(req_)> io_{&ctx_};
    };

    class RouteChatRequest : public RequestBase {
    public:

        RouteChatRequest(EverythingSvr& owner)
            : RequestBase(owner) {

            owner_.grpc().service_.RequestRouteChat(&ctx_, &stream_, cq(), cq(),
                in_handle_.tag(Handle::Operation::CONNECT,
                    [this, &owner](bool ok, Handle::Operation /* op */) {

                        LOG_DEBUG << me(*this) << " - Processing a new connect from " << ctx_.peer();

                        if (!ok) [[unlikely]] {
                            // The operation failed.
                            // Let's end it here.
                            LOG_WARN << "The request-operation failed. Assuming we are shutting down";
                            return;
                        }

                        // Before we do anything else, we must create a new instance
                        // so the service can handle a new request from a client.
                        owner_.createNew<RouteChatRequest>(owner);

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
            }));
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
            // Cute! the Read operation takes a pointer
            stream_.Read(&req_, in_handle_.tag(
                Handle::Operation::READ,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                    // The operation failed.
                    // This is normal on an incoming stream, when there are no more messages.
                    // As far as I know, there is no way at this point to deduce if the false status is
                    // because the client is done sending messages, or because we encountered
                    // an error.
                    LOG_TRACE << "The read-operation failed. It's probably not an error :)";

                    done_reading_ = true;
                    return finishIfDone();
                    }

                    read(false); // Initiate the read for the next incoming message
            }));
        }

        void write(const bool first) {
            if (!first) {
                reply_.Clear();
            }

            if (++replies_ > owner_.config().num_stream_messages) {
                done_writing_ = true;

                LOG_TRACE << me(*this) << " - We are done writing to the stream.";
                return finishIfDone();
            }

            // This is where we are ready to write a new message.
            // If this was code for a framework, this is where we would have called
            // the `onRpcRequestRouteChatReadytoSendNewMessage()` method, or unblocked
            // the next statement in a co-routine awaiting the next state-change.

            reply_.set_message(std::string{"Server Message #"} + std::to_string(replies_));

            // Start new write
            stream_.Write(reply_, out_handle_.tag(
                                Handle::Operation::WRITE,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        LOG_WARN << "The write-operation failed.";

                        // When ok is false here, we will not be able to write
                        // anything on this stream.
                        done_writing_ = true;
                        return finishIfDone();
                    }

                    write(false);
                }));
        }

        // We wait until all incoming messages are received and all outgoing messages are sent
        // before we send the finish message.
        void finishIfDone() {
            if (!sent_finish_ && done_reading_ && done_writing_) {
                LOG_TRACE << me(*this) << " - We are done reading and writing. Sending finish!";

                stream_.Finish(grpc::Status::OK, out_handle_.tag(
                    Handle::Operation::FINISH,
                    [this](bool ok, Handle::Operation /* op */) {

                        if (!ok) [[unlikely]] {
                            LOG_WARN << "The finish-operation failed.";
                        }

                        LOG_TRACE << me(*this) << " - We are done";
                }));
                sent_finish_ = true;
                return;
            }
        }

        bool done_reading_ = false;
        bool done_writing_ = false;
        bool sent_finish_ = false;
        size_t replies_ = 0;

        // We are streaming messages in and out simultaneously, so we need two handles.
        // One for each direction.
        Handle in_handle_{*this};
        Handle out_handle_{*this};

        ::grpc::ServerContext ctx_;
        ::routeguide::RouteNote req_;
        ::routeguide::RouteNote reply_;

        // Interestingly, the template the class is named `*ReaderWriter`, while
        // the template argument order is first Writer type and then Reader type.
        // Lot's of room for false assumptions and subtle errors here ;)
        ::grpc::ServerAsyncReaderWriter< decltype(reply_), decltype(req_)> stream_{&ctx_};
    };

    EverythingSvr(const Config& config)
        : EventLoopBase(config) {

        grpc::ServerBuilder builder;
        builder.AddListeningPort(config_.address, grpc::InsecureServerCredentials());
        builder.RegisterService(&grpc_.service_);
        grpc_.cq_ = builder.AddCompletionQueue();
        // Finally assemble the server.
        grpc_.server_ = builder.BuildAndStart();

        LOG_INFO
            // Fancy way to print the class-name.
            // Useful when I copy/paste this code around ;)
            << boost::typeindex::type_id_runtime(*this).pretty_name()

            // The useful information
            << " listening on " << config_.address;

        // Prepare the first instances of request handlers
        createNew<GetFeatureRequest>(*this);
        createNew<ListFeaturesRequest>(*this);
        createNew<RecordRouteRequest>(*this);
        createNew<RouteChatRequest>(*this);
    }
};
