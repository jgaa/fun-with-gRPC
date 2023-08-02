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
 * \brief The UnaryAndSingleStreamSvc class
 *
 * This class implements:
 *  - GetFeature()
 *  - ListFeatures()
 *  - RecordRoute()
 */

class UnaryAndSingleStreamSvc {
public:

    // Create and start a new instance of a request-type
    template <typename T>
    static void createNew(UnaryAndSingleStreamSvc& parent,
                          ::routeguide::RouteGuide::AsyncService& service,
                          ::grpc::ServerCompletionQueue& cq) {

        // Use make_uniqe, so we destroy the object if it throws an exception
        // (for example out of memory).
        try {
            new T(parent, service, cq);

            // If we got here, the instance should be fine, so let it handle itself.
        } catch(const std::exception& ex) {
            LOG_ERROR << "Got exception while creating a new instance. "
                      << "This will end my possibility to handle any further requests. "
                      << " Error: " << ex.what();
        }
    }

    /*! Base class for requests
     *
     *  In order to use `this` as a tag and avoid any special processing in the
     *  event-loop, the simplest approach in C++ is to let the request implementations
     *  inherit form a base-class that contains the shared code they all need, and
     *  a pure virtual method for the state-machine.
     */
    class RequestBase {
    public:
        RequestBase(UnaryAndSingleStreamSvc& parent,
                    ::routeguide::RouteGuide::AsyncService& service,
                    ::grpc::ServerCompletionQueue& cq)
            : parent_{parent}, service_{service}, cq_{cq} {}

        virtual ~RequestBase() = default;

        // The state-machine
        virtual void proceed(bool ok) = 0;

        void done() {
            // Ugly, ugly, ugly
            LOG_TRACE << "If the program crash now, it was a bad idea to delete this ;)";
            delete this;
        }


    protected:
        // The state required for all requests
        UnaryAndSingleStreamSvc& parent_;
        ::routeguide::RouteGuide::AsyncService& service_;
        ::grpc::ServerCompletionQueue& cq_;
        ::grpc::ServerContext ctx_;
    };

    /*! Implementation for the `GetFeature()` RPC call.
     */
    class GetFeatureRequest : public RequestBase {
    public:
        enum class State {
            CREATED,
            REPLIED,
            DONE
        };

        GetFeatureRequest(UnaryAndSingleStreamSvc& parent,
                          ::routeguide::RouteGuide::AsyncService& service,
                          ::grpc::ServerCompletionQueue& cq)
            : RequestBase(parent, service, cq) {

            // Register this instance with the event-queue and the service.
            // The first event received over the queue is that we have a request.
            service_.RequestGetFeature(&ctx_, &req_, &resp_, &cq_, &cq_, this);
        }

        // State-machine to deal with a single request
        // This works almost like a co-routine, where we work our way down for each
        // time we are called. The State_ could just as well have been an integer/counter;
        void proceed(bool ok) override {
            switch(state_) {
            case State::CREATED:
                if (!ok) [[unlikely]] {
                    // The operation failed.
                    // Let's end it here.
                    LOG_WARN << "The request-operation failed. Assuming we are shutting down";
                    return done();
                }

                // Before we do anything else, we must create a new instance of
                // OneRequest, so the service can handle a new request from a client.
                createNew<GetFeatureRequest>(parent_, service_, cq_);

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
                resp_.Finish(reply_, ::grpc::Status::OK, this);

                // This instance is now active.
                state_ = State::REPLIED;
                // Now, we wait for a new event...
                break;

            case State::REPLIED:
                if (!ok) [[unlikely]] {
                    // The operation failed.
                    LOG_WARN << "The reply-operation failed.";
                }

                state_ = State::DONE; // Not required, but may be useful if we investigate a crash.

                // We are done. There will be no further events for this instance.
                return done();

            default:
                LOG_ERROR << "Logic error / unexpected state in proceed()!";
            } // switch
        }

    private:
        ::routeguide::Point req_;
        ::routeguide::Feature reply_;
        ::grpc::ServerAsyncResponseWriter<::routeguide::Feature> resp_{&ctx_};
        State state_ = State::CREATED;
    };


    /*! Implementation for the `ListFeatures()` RPC call.
     *
     *  This is a bit more advanced. We receive a normal request message,
     *  but the reply is a stream of messges.
     */
    class ListFeaturesRequest : public RequestBase {
    public:
        enum class State {
            CREATED,
            REPLYING,
            FINISHING,
            DONE
        };

        ListFeaturesRequest(UnaryAndSingleStreamSvc& parent,
                            ::routeguide::RouteGuide::AsyncService& service,
                            ::grpc::ServerCompletionQueue& cq)
            : RequestBase(parent, service, cq) {

            // Register this instance with the event-queue and the service.
            // The first event received over the queue is that we have a request.
            service_.RequestListFeatures(&ctx_, &req_, &resp_, &cq_, &cq_, this);
        }

        // State-machine to deal with a single request
        // This works almost like a co-routine, where we work our way down for each
        // time we are called. The State_ could just as well have been an integer/counter;
        void proceed(bool ok) override {
            switch(state_) {
            case State::CREATED:
                if (!ok) [[unlikely]] {
                    // The operation failed.
                    // Let's end it here.
                    LOG_WARN << "The request-operation failed. Assuming we are shutting down";
                    return done();
                }

                // Before we do anything else, we must create a new instance
                // so the service can handle a new request from a client.
                createNew<ListFeaturesRequest>(parent_, service_, cq_);

                state_ = State::REPLYING;
                //fallthrough

            case State::REPLYING:
                if (!ok) [[unlikely]] {
                    // The operation failed.
                    LOG_WARN << "The reply-operation failed.";
                }

                if (++replies_ > parent_.config_.num_stream_messages) {
                    // We have reached the desired number of replies
                    state_ = State::FINISHING;

                    // *Finish* will relay the event that the write is completed on the queue, using *this* as tag.
                    resp_.Finish(::grpc::Status::OK, this);

                    // Now, wait for the client to be aware of use finishing.
                    break;
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

                // *Write* will relay the event that the write is completed on the queue, using *this* as tag.
                resp_.Write(reply_, this);

                // Now, we wait for the write to complete
                break;

            case State::FINISHING:
                if (!ok) [[unlikely]] {
                    // The operation failed.
                    LOG_WARN << "The finish-operation failed.";
                }

                state_ = State::DONE; // Not required, but may be useful if we investigate a crash.

                // We are done. There will be no further events for this instance.
                return done();

            default:
                LOG_ERROR << "Logic error / unexpected state in proceed()!";
            } // switch
        }

    private:
        ::routeguide::Rectangle req_;
        ::routeguide::Feature reply_;
        ::grpc::ServerAsyncWriter<::routeguide::Feature> resp_{&ctx_};
        State state_ = State::CREATED;
        size_t replies_ = 0;
    };


    /*! Implementation for the `RecordRouteRequest()` RPC call.
     *
     *  This is a bit more advanced. We receive a normal request message,
     *  but the reply is a stream of messages.
     */
    class RecordRouteRequest : public RequestBase {
    public:
        enum class State {
            CREATED,
            READING,
            FINISHING,
            DONE
        };

        RecordRouteRequest(UnaryAndSingleStreamSvc& parent,
                            ::routeguide::RouteGuide::AsyncService& service,
                            ::grpc::ServerCompletionQueue& cq)
            : RequestBase(parent, service, cq) {

            // Register this instance with the event-queue and the service.
            // The first event received over the queue is that we have a request.
            service_.RequestRecordRoute(&ctx_, &reader_, &cq_, &cq_, this);
        }

        // State-machine to deal with a single request
        // This works almost like a co-routine, where we work our way down (or repeat) for each
        // time we are called. The State_ could just as well have been an integer/counter;
        void proceed(bool ok) override {
            switch(state_) {
            case State::CREATED:
                if (!ok) [[unlikely]] {
                    // The operation failed.
                    // Let's end it here.
                    LOG_WARN << "The request-operation failed.";
                    return done();
                }

                // Before we do anything else, we must create a new instance
                // so the service can handle a new request from a client.
                createNew<RecordRouteRequest>(parent_, service_, cq_);

                // Initiate the first read operation
                reader_.Read(&req_, this);
                state_ = State::READING;
                break;

            case State::READING:
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
                    reader_.Finish(reply_, ::grpc::Status::OK, this);
                    state_ = State::FINISHING;
                    break;
                }

                // This is where we have read a message from the request.
                // If this was code for a framework, this is where we would have called
                // the `onRpcRequestRecordRouteGotMessage()` method, or unblocked the next statement
                // in a co-routine awaiting the next state-change.
                //
                // In our case, let's just log it.
                LOG_TRACE << "Got message: longitude=" << req_.longitude()
                          << ", latitude=" << req_.latitude();

                // Prepare the reply-object to be re-used.
                // This is usually cheaper than creating a new one for each read operation.
                req_.Clear();

                // *Read* will relay the event that the write is completed on the queue, using *this* as tag.
                // Initiate the first read operation
                reader_.Read(&req_, this);

                // Now, we wait for the read to complete
                break;

            case State::FINISHING:
                if (!ok) [[unlikely]] {
                    // The operation failed.
                    LOG_WARN << "The finish-operation failed.";
                }

                state_ = State::DONE; // Not required, but may be useful if we investigate a crash.

                // We are done. There will be no further events for this instance.
                return done();

            default:
                LOG_ERROR << "Logic error / unexpected state in proceed()!";
            } // switch
        }

    private:
        ::routeguide::Point req_;
        ::routeguide::RouteSummary reply_;
        ::grpc::ServerAsyncReader< ::routeguide::RouteSummary, ::routeguide::Point> reader_{&ctx_};
        State state_ = State::CREATED;
    };


    UnaryAndSingleStreamSvc(const Config& config)
        : config_{config} {}

    void init() {
        grpc::ServerBuilder builder;
        builder.AddListeningPort(config_.address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service_);
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

    void run() {
        init();

       // Prepare for the first request for each reqest type.
       createNew<GetFeatureRequest>(*this, service_, *cq_);
       createNew<ListFeaturesRequest>(*this, service_, *cq_);
       createNew<RecordRouteRequest>(*this, service_, *cq_);

       // The inner event-loop
       while(true) {
           bool ok = true;
           void *tag = {};

           // FIXME: This is crazy. Figure out how to use stable clock!
           const auto deadline = std::chrono::system_clock::now()
                                 + std::chrono::milliseconds(1000);

           // Get any IO operation that is ready.
           const auto status = cq_->AsyncNext(&tag, &ok, deadline);
           LOG_TRACE << "async-next: ok=" << ok
                     << ", status=" << status
                     << ", tag=" << tag;

           // So, here we deal with the first of the three states: The status from Next().
           switch(status) {
           case grpc::CompletionQueue::NextStatus::TIMEOUT:
               LOG_DEBUG << "AsyncNext() timed out.";
               continue;

           case grpc::CompletionQueue::NextStatus::GOT_EVENT:
               LOG_DEBUG << "AsyncNext() returned an event. The status is "
                         << (ok ? "OK" : "FAILED");

               // Use a scope to allow a new variable inside a case statement.
               {
                   auto request = static_cast<RequestBase *>(tag);

                   // Now, let the OneRequest state-machine deal with the event.
                   // We could have done it here, but that code would smell really nasty.
                   request->proceed(ok);
               }
               break;

           case grpc::CompletionQueue::NextStatus::SHUTDOWN:
               LOG_INFO << "SHUTDOWN. Tearing down the gRPC connection(s) ";
               return;
           } // switch
       } // loop
    }

    void stop() {
        LOG_INFO << "Shutting down "
                 << boost::typeindex::type_id_runtime(*this).pretty_name();
        server_->Shutdown();
        server_->Wait();
    }

private:
    // An instance of our service, compiled from code generated by protoc
    ::routeguide::RouteGuide::AsyncService service_;

    // This is the Queue. It's shared for all the requests.
    std::unique_ptr<grpc::ServerCompletionQueue> cq_;

    // A gRPC server object
    std::unique_ptr<grpc::Server> server_;

    // Config, so the user can override our default parameters
    const Config config_;
};
