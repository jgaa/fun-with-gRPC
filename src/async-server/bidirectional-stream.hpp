
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
                    if (!ok) [[unlikely]] {
                        // The operation failed.
                        // Let's end it here.
                        LOG_WARN << "The request-operation failed. Assuming we are shutting down";
                        return;
                    }

                    // Before we do anything else, we must create a new instance of
                    // OneRequest, so the service can handle a new request from a client.
                    // Note that we institiate with `owner`, not `owner_`, as `owner`
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

                // Prepare the reply-object to be re-used.
                // This is usually cheaper than creating a new one for each write operation.

                // *Write* will relay the event that the write is completed on the queue, using *this* as tag.
                // Initiate the first read operation

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
    }
};
