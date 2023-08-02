#pragma once

#include <grpcpp/grpcpp.h>

#include "funwithgrpc/Config.h"

#include "route_guide.grpc.pb.h"
#include "funwithgrpc/logging.h"


class SimpleReqResClient {
public:

    class OneRequest {
    public:
        OneRequest(SimpleReqResClient& parent)
                : parent_{parent} {

            // Initiate the async request.
            rpc_ = parent_.stub_->AsyncGetFeature(&ctx_, req_, &parent_.cq_);
            assert(rpc_);

            // Add the operation to the queue, so we get notified when
            // the request is completed.
            // Note that we use `this` as tag.
            rpc_->Finish(&reply_, &status_, this);

            // Reference-counting of instances of requests in flight
            parent.incCounter();
        }

        void proceed(bool ok) {
            if (!ok) [[unlikely]] {
                LOG_WARN << "OneRequest: The request failed.";
                return done();
            }

            // Initiate a new request
            parent_.createRequest();

            if (status_.ok()) {
                LOG_TRACE << "Request successful. Message: " << reply_.name();
            } else {
                LOG_WARN << "OneRequest: The request failed with error-message: " << status_.error_message();
            }

            // The reply is a single message, so at this time we are done.
            done();
        }

    private:
        void done() {
            // Ugly, ugly, ugly
            LOG_TRACE << "If the program crash now, it was a bad idea to delete this ;)";

            // Reference-counting of instances of requests in flight
            parent_.decCounter();
            delete this;
        }

        SimpleReqResClient& parent_;

        // We need quite a few variables to perform our single RPC call.
        ::routeguide::Point req_;
        ::routeguide::Feature reply_;
        ::grpc::Status status_;
        std::unique_ptr< ::grpc::ClientAsyncResponseReader< ::routeguide::Feature>> rpc_;
        ::grpc::ClientContext ctx_;
    };
    
    SimpleReqResClient(const Config& config)
        : config_{config} {}

    // Run the event-loop.
    // Returns when there are no more requests to send
    void run() {

        LOG_INFO << "Connecting to gRPC service at: " << config_.address;
        channel_ = grpc::CreateChannel(config_.address, grpc::InsecureChannelCredentials());

        // Is it a "lame channel"?
        // In stead of returning an empty object if something went wrong,
        // the gRPC team decided it was a better idea to return a valid object with
        // an invalid state that will fail any real operations.
        if (auto status = channel_->GetState(false); status == GRPC_CHANNEL_TRANSIENT_FAILURE) {
            LOG_TRACE << "run - Failed to initialize channel. Is the server address even valid?";
            return;
        }

        // For some reason, the code below always fail.
        // So, don't use GetState to see if you can connect to the server.
//        if (auto status = channel_->GetState(true); status != GRPC_CHANNEL_READY) {
//            LOG_ERROR << "Failed to connect to " << serverAddress;
//            return;
//        }

        stub_ = ::routeguide::RouteGuide::NewStub(channel_);
        assert(stub_);

        // Add request(s)
        for(auto i = 0; i < config_.parallel_requests;  ++i) {
            createRequest();
        }

        while(pending_requests_) {
            // FIXME: This is crazy. Figure out how to use stable clock!
            const auto deadline = std::chrono::system_clock::now()
                                  + std::chrono::milliseconds(500);

            // Get any IO operation that is ready.
            void * tag = {};
            bool ok = true;

            // Wait for the next event to complete in the queue
            const auto status = cq_.AsyncNext(&tag, &ok, deadline);

            // So, here we deal with the first of the three states: The status of Next().
            switch(status) {
            case grpc::CompletionQueue::NextStatus::TIMEOUT:
                LOG_DEBUG << "AsyncNext() timed out.";
                continue;

            case grpc::CompletionQueue::NextStatus::GOT_EVENT:
                LOG_TRACE << "AsyncNext() returned an event. The boolean status is "
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
        } // event-loop
    }

    void close() {
        cq_.Shutdown();
    }

    void createRequest() {
        if (++request_count > config_.num_requests) {
            LOG_TRACE << "We have already started " << config_.num_requests << " requests.";
            return; // We are done
        }

        try {
            new OneRequest(*this);
        } catch (const std::exception& ex) {
            LOG_ERROR << "Got exception while creating a new instance. Error: "
                      << ex.what();
        }
    }

    void incCounter() {
        ++pending_requests_;
    }

    void decCounter() {
        assert(pending_requests_ >= 1);
        --pending_requests_;
    }

private:
    // This is the Queue. It's shared for all the requests.
    ::grpc::CompletionQueue cq_;

    // This is a connection to the gRPC server
    std::shared_ptr<grpc::Channel> channel_;

    // An instance of the client that was generated from our .proto file.
    std::unique_ptr<::routeguide::RouteGuide::Stub> stub_;
    
    const Config& config_;
    std::atomic_size_t pending_requests_{0};
    std::atomic_size_t request_count{0};
};
