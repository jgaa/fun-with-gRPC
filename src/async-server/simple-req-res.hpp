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
 * \brief The SimpleReqRespSvc class
 *
 * This class implements just the basic unary RPC operation `GetFeature()`.
 *
 * This code is "inspired" by https://github.com/grpc/grpc/blob/v1.56.0/examples/cpp/helloworld/greeter_async_server.cc
 */

class SimpleReqRespSvc {
public:
    class OneRequest {
    public:
        enum class State {
            CREATED,
            REPLIED,
            DONE
        };

        OneRequest(::routeguide::RouteGuide::AsyncService& service,
                   ::grpc::ServerCompletionQueue& cq)
            : service_{service}, cq_{cq} {

            // Register this instance with the event-queue and the service.
            // The first event received over the queue is that we have a request.
            service_.RequestGetFeature(&ctx_, &req_, &resp_, &cq_, &cq_, this);
        }

        // State-machine to deal with a single request
        // This works almost like a co-routine, where we work our way down for each
        // time we are called. The State_ could just as well have been an integer/counter;
        void proceed(bool ok) {
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
                createNew(service_, cq_);

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
                assert(false);
            } // switch
        }

        void done() {
            // Ugly, ugly, ugly
            LOG_TRACE << "If the program crash now, it was a bad idea to delete this ;)";
            delete this;
        }

        // Create and start a new instance
        static void createNew(::routeguide::RouteGuide::AsyncService& service,
                              ::grpc::ServerCompletionQueue& cq) {

            // Use make_uniqe, so we destroy the object if it throws an exception
            // (for example out of memory).
            try {
                new OneRequest(service, cq);

                // If we got here, the instance should be fine, so let it handle itself.
            } catch(const std::exception& ex) {
                LOG_ERROR << "Got exception while creating a new instance. "
                          << "This will end my possibility to handle any further requests. "
                          << " Error: " << ex.what();
            }
        }

    private:
        // We need many variables to handle this one RPC call...
        ::routeguide::RouteGuide::AsyncService& service_;
        ::grpc::ServerCompletionQueue& cq_;
        ::routeguide::Point req_;
        ::grpc::ServerContext ctx_;
        ::routeguide::Feature reply_;
        ::grpc::ServerAsyncResponseWriter<::routeguide::Feature> resp_{&ctx_};
        State state_ = State::CREATED;
    };


    SimpleReqRespSvc(Config& config)
        : config_{config} {}

    void init() {
        grpc::ServerBuilder builder;

        // Tell gRPC what TCP address/port to listen to and how to handle TLS.
        // grpc::InsecureServerCredentials() will use HTTP 2.0 without encryption.
        builder.AddListeningPort(config_.address, grpc::InsecureServerCredentials());

        // Tell gRPC what rpc methods we support.
        // The code for the class exposed by `service_` is generated from our proto-file.
        builder.RegisterService(&service_);

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

    void run() {
        init();

        // Prepare for the first request.
        OneRequest::createNew(service_, *cq_);

        // The inner event-loop
        while(true) {
            bool ok = true;
            void *tag = {};

            // FIXME: This is crazy. Figure out how to use stable clock!
            const auto deadline = std::chrono::system_clock::now()
                                  + std::chrono::milliseconds(1000);

            // Get the event for any async operation that is ready.
            const auto status = cq_->AsyncNext(&tag, &ok, deadline);

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
                    auto request = static_cast<OneRequest *>(tag);

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

    const Config& config_;
};
