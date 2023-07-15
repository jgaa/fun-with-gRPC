#pragma once

#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>

#include "funwithgrpc/BaseRequest.hpp"
#include "route_guide.grpc.pb.h"
#include "funwithgrpc/logging.h"
#include "funwithgrpc/Config.h"


class EverythingClient
    : public EventLoopBase<ClientVars<::routeguide::RouteGuide>> {
public:

    template <typename reqT>
    void createNext() {
        if (++request_count_ > config_.num_requests) {
            LOG_TRACE << "We have already started " << config_.num_requests << " requests.";
            return; // We are done
        }

        createNew<reqT>(*this);
    }

    class GetFeatureRequest : public RequestBase {
    public:

        GetFeatureRequest(EverythingClient& owner)
            : RequestBase(owner) {

            LOG_DEBUG << me(*this) << " - Connecting...";

            // Initiate the async request.
            rpc_ = owner.grpc().stub_->AsyncGetFeature(&ctx_, req_, cq());
            assert(rpc_);

            // Add the operation to the queue. We will be notified when
            // the request is completed.
            rpc_->Finish(&reply_, &status_, handle_.tag(
                Handle::Operation::FINISH,
                [this, &owner](bool ok, Handle::Operation /* op */) {

                    if (!ok) [[unlikely]] {
                    LOG_WARN << me(*this) << " - The request failed. Status: " << status_.error_message();
                        return;
                    }

                    if (status_.ok()) {
                        LOG_TRACE << me(*this) << " - Request successful. Message: " << reply_.name();

                        owner.createNext<GetFeatureRequest>();
                    } else {
                        LOG_WARN << me(*this) << " - The request failed with error-message: "
                                 << status_.error_message();
                    }
                }));
        }

    private:
        Handle handle_{*this};

        // We need quite a few variables to perform our single RPC call.
        ::grpc::ClientContext ctx_;
        ::routeguide::Point req_;
        ::routeguide::Feature reply_;
        ::grpc::Status status_;
        std::unique_ptr< ::grpc::ClientAsyncResponseReader<decltype(reply_)>> rpc_;

    }; // GetFeatureRequest



    class ListFeaturesRequest : public RequestBase {
    public:

        ListFeaturesRequest(EverythingClient& owner)
            : RequestBase(owner) {

            LOG_DEBUG << me(*this) << " - Connecting...";

            // Initiate the async request.
            rpc_ = owner.grpc().stub_->AsyncListFeatures(&ctx_, req_, cq(), op_handle_.tag(
                Handle::Operation::CONNECT,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << me(*this) << " - The request failed (connect).";
                        return;
                    }

                    read(true);
            }));

            assert(rpc_);
            rpc_->Finish(&status_, finish_handle_.tag(
                Handle::Operation::FINISH,
                [this](bool ok, Handle::Operation /* op */) mutable {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << me(*this) << " - The request failed (connect).";
                        return;
                    }

                    if (status_.ok()) {
                        LOG_TRACE << me(*this) << " - Initiating a new request";
                        static_cast<EverythingClient&>(owner_).createNext<ListFeaturesRequest>();
                    } else {
                        LOG_WARN << me(*this) << " - The request finished with error-message: "
                                 << status_.error_message();
                    }
            }));
        }

    private:
        void read(const bool first) {

            if (!first) {
                // This is where we have an actual message from the server.
                // If this was a framework, this is where we would have called
                // `onListFeatureReceivedOneMessage()` or or unblocked the next statement
                // in a co-routine waiting for the next request

                // In our case, let's just log it.
                LOG_TRACE << me(*this) << " - Request successful. Message: " << reply_.name();

                // Prepare the reply-object to be re-used.
                // This is usually cheaper than creating a new one for each read operation.
                reply_.Clear();
            }

            // Now, lets register another read operation
            rpc_->Read(&reply_, op_handle_.tag(
                                    Handle::Operation::READ,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        LOG_TRACE << me(*this) << " - The read-request failed.";
                        return;
                    }

                    read(false);
                }));
        }

        Handle op_handle_{*this};
        Handle finish_handle_{*this};

        ::grpc::ClientContext ctx_;
        ::routeguide::Rectangle req_;
        ::routeguide::Feature reply_;
        ::grpc::Status status_;
        std::unique_ptr< ::grpc::ClientAsyncReader< decltype(reply_)>> rpc_;
    }; // ListFeaturesRequest


    class RecordRouteRequest : public RequestBase {
    public:

        RecordRouteRequest(EverythingClient& owner)
            : RequestBase(owner) {

            LOG_DEBUG << me(*this) << " - Connecting...";

            // Initiate the async request (connect).
            rpc_ = owner.grpc().stub_->AsyncRecordRoute(&ctx_, &reply_, cq(), io_handle_.tag(
                Handle::Operation::CONNECT,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << me(*this) << " - The request failed (connect).";
                        return;
                    }

                    // The server will not send anything until we are done writing.
                    // So let's get started.

                    write(true);
               }));

            // Register a handler to be called when the server has sent a reply and final status.
            assert(rpc_);
            rpc_->Finish(&status_, finish_handle_.tag(
                Handle::Operation::FINISH,
                [this](bool ok, Handle::Operation /* op */) mutable {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << me(*this) << " - The request failed (connect).";
                        return;
                    }

                    if (status_.ok()) {
                        LOG_TRACE << me(*this) << " - Initiating a new request";
                        static_cast<EverythingClient&>(owner_).createNext<RouteChatRequest>();
                    } else {
                        LOG_WARN << me(*this) << " - The request finished with error-message: "
                                 << status_.error_message();
                    }
               }));
        }

    private:
        void write(const bool first) {

            if (!first) {
                req_.Clear();
            }

            if (++sent_messages_ > owner_.config().num_stream_messages) {

                LOG_TRACE << me(*this) << " - We are done writing to the stream.";

                rpc_->WritesDone(io_handle_.tag(
                    Handle::Operation::WRITE_DONE,
                    [this](bool ok, Handle::Operation /* op */) {
                        if (!ok) [[unlikely]] {
                            LOG_TRACE << me(*this) << " - The writes-done request failed.";
                            return;
                        }

                        LOG_TRACE << me(*this) << " - We have told the server that we are done writing.";
                    }));

                return;
            }

            // Send some data to the server
            req_.set_latitude(100);
            req_.set_longitude(sent_messages_);

            // Now, lets register another write operation
            rpc_->Write(req_, io_handle_.tag(
                Handle::Operation::WRITE,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        LOG_TRACE << me(*this) << " - The write-request failed.";
                        return;
                    }

                    write(false);
                }));
        }

        Handle io_handle_{*this};
        Handle finish_handle_{*this};
        size_t sent_messages_ = 0;

        ::grpc::ClientContext ctx_;
        ::routeguide::Point req_;
        ::routeguide::RouteSummary reply_;
        ::grpc::Status status_;
        std::unique_ptr<  ::grpc::ClientAsyncWriter< ::routeguide::Point>> rpc_;
    }; // RecordRouteRequest


    class RouteChatRequest : public RequestBase {
    public:

        RouteChatRequest(EverythingClient& owner)
            : RequestBase(owner) {

            LOG_DEBUG << me(*this) << " - Connecting...";

            // Initiate the async request.
            rpc_ = owner.grpc().stub_->AsyncRouteChat(&ctx_, cq(), in_handle_.tag(
                Handle::Operation::CONNECT,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << me(*this) << " - The request failed (connect).";
                        return;
                    }

                    // We are initiating both reading and writing.
                    // Some clients may initiate only a read or a write at this time,
                    // depending on the use-case.
                    read(true);
                    write(true);
                }));

            assert(rpc_);
            rpc_->Finish(&status_, finish_handle_.tag(
                Handle::Operation::FINISH,
                [this](bool ok, Handle::Operation /* op */) mutable {
                    if (!ok) [[unlikely]] {
                        LOG_WARN << me(*this) << " - The request failed (connect).";
                        return;
                    }

                    if (status_.ok()) {
                        LOG_TRACE << me(*this) << " - Initiating a new request";
                        static_cast<EverythingClient&>(owner_).createNext<RouteChatRequest>();
                    } else {
                        LOG_WARN << me(*this) << " - The request finished with error-message: "
                                 << status_.error_message();
                   }
                }));
        }

    private:
        void read(const bool first) {

            if (!first) {
                // This is where we have an actual message from the server.
                // If this was a framework, this is where we would have called
                // `onListFeatureReceivedOneMessage()` or or unblocked the next statement
                // in a co-routine waiting for the next request

                // In our case, let's just log it.
                LOG_TRACE << me(*this) << " - Request successful. Message: " << reply_.message();
                reply_.Clear();
            }

            // Now, lets register another read operation
            rpc_->Read(&reply_, in_handle_.tag(
                Handle::Operation::READ,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        LOG_TRACE << me(*this) << " - The read-request failed.";
                        return;
                    }

                    read(false);
                }));
        }

        void write(const bool first) {

            if (!first) {
                req_.Clear();
            }

            if (++sent_messages_ > owner_.config().num_stream_messages) {

                LOG_TRACE << me(*this) << " - We are done writing to the stream.";

                rpc_->WritesDone(out_handle_.tag(
                    Handle::Operation::WRITE_DONE,
                    [this](bool ok, Handle::Operation /* op */) {
                        if (!ok) [[unlikely]] {
                            LOG_TRACE << me(*this) << " - The writes-done request failed.";
                            return;
                        }

                        LOG_TRACE << me(*this) << " - We have told the server that we are done writing.";
                  }));

                return;
            }

            // Now, lets register another write operation
            rpc_->Write(req_, out_handle_.tag(
                Handle::Operation::WRITE,
                [this](bool ok, Handle::Operation /* op */) {
                    if (!ok) [[unlikely]] {
                        LOG_TRACE << me(*this) << " - The write-request failed.";
                        return;
                    }

                    write(false);
                }));
        }

        Handle in_handle_{*this};
        Handle out_handle_{*this};
        Handle finish_handle_{*this};
        size_t sent_messages_ = 0;

        ::grpc::ClientContext ctx_;
        ::routeguide::RouteNote req_;
        ::routeguide::RouteNote reply_;
        ::grpc::Status status_;
        std::unique_ptr<  ::grpc::ClientAsyncReaderWriter< ::routeguide::RouteNote, ::routeguide::RouteNote>> rpc_;
    }; // ListFeaturesRequest


    EverythingClient(const Config& config)
        : EventLoopBase(config) {


        LOG_INFO << "Connecting to gRPC service at: " << config.address;
        grpc_.channel_ = grpc::CreateChannel(config.address, grpc::InsecureChannelCredentials());

        // Is it a "lame channel"?
        // In stead of returning an empty object if something went wrong,
        // the gRPC team decided it was a better idea to return a valid object with
        // an invalid state that will fail any real operations.
        if (auto status = grpc_.channel_->GetState(false); status == GRPC_CHANNEL_TRANSIENT_FAILURE) {
            LOG_TRACE << "run - Failed to initialize channel. Is the server address even valid?";
            throw std::runtime_error{"Failed to initialize channel"};
        }

        grpc_.stub_ = ::routeguide::RouteGuide::NewStub(grpc_.channel_);
        assert(grpc_.stub_);

        // Add request(s)
        LOG_DEBUG << "Creating " << config_.parallel_requests
                  << " initial request(s) of type " << config_.request_type;

        for(auto i = 0; i < config_.parallel_requests;  ++i) {
            nextRequest();
        }
    }


private:
    void nextRequest() {
        static const std::array<std::function<void()>, 4> request_variants = {
            [this]{createNext<GetFeatureRequest>();},
            [this]{createNext<ListFeaturesRequest>();},
            [this]{createNext<RecordRouteRequest>();},
            [this]{createNext<RouteChatRequest>();},
        };

        request_variants.at(config_.request_type)();
    }

    size_t request_count_{0};
};
