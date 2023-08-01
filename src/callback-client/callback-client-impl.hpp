#pragma once

#include <atomic>
#include <future>
#include <deque>

#include <boost/type_index.hpp>
#include <boost/type_index/runtime_cast/register_runtime_class.hpp>

#include <grpcpp/grpcpp.h>
#include <grpcpp/alarm.h>

#include "route_guide.grpc.pb.h"
#include "funwithgrpc/logging.h"
#include "funwithgrpc/Config.h"


class EverythingCallbackClient {
public:

    /*! RIAA reference-counting of requests in flight.
     *
     * When there are no more in flight, we signal the
     * future that holds the thread in run().
     */
    struct Base {
        Base(EverythingCallbackClient& owner)
            : owner_{owner} {
            ++owner_.in_flight_;
        }

        ~Base() {
            if (--owner_.in_flight_ == 0) {

                // Release the hold in `run()`
                owner_.done_.set_value();
            }
        }

        EverythingCallbackClient& owner_;
    };

    EverythingCallbackClient(const Config& config)
        : config_{config} {

        LOG_INFO << "Connecting to gRPC service at: " << config.address;
        channel_ = grpc::CreateChannel(config.address, grpc::InsecureChannelCredentials());

        // Is it a "lame channel"?
        // In stead of returning an empty object if something went wrong,
        // the gRPC team decided it was a better idea to return a valid object with
        // an invalid state that will fail any real operations.
        if (auto status = channel_->GetState(false); status == GRPC_CHANNEL_TRANSIENT_FAILURE) {
            LOG_TRACE << "run - Failed to initialize channel. Is the server address even valid?";
            throw std::runtime_error{"Failed to initialize channel"};
        }

        stub_ = ::routeguide::RouteGuide::NewStub(channel_);
        assert(stub_);
    }

    using get_feature_cb_t = std::function<void(const grpc::Status& status,
                                                const ::routeguide::Feature& feature)>;
    void getFeature(::routeguide::Point& point, get_feature_cb_t && fn) {

        // In order to keep the state for the duration of the async request,
        // we use this class.
        class Impl : Base {
        public:
            Impl(EverythingCallbackClient& owner,
                 ::routeguide::Point& point,
                 get_feature_cb_t && fn)
                : Base(owner), req_{std::move(point)}, caller_callback_{std::move(fn)} {

                LOG_TRACE << "getFeature starting async request.";

                owner_.stub_->async()->GetFeature(&ctx_, &req_, &reply_,
                                           [this](grpc::Status status) {

                    LOG_TRACE << "getFeature calling finished callback.";
                    caller_callback_(status, reply_);
                    delete this;
                });
            }

        private:
            grpc::ClientContext ctx_;
            ::routeguide::Point req_;
            ::routeguide::Feature reply_;
            get_feature_cb_t caller_callback_;
        };

        // Shorthand for `new Impl(...)` but with exception guarantee.
        std::make_unique<Impl>(*this, point, std::move(fn)).release();
    }


    using feature_or_status_t = std::variant<
            // I would have preferred a reference, but that don't work in C++ 20 in variants
            const ::routeguide::Feature *,
            grpc::Status
        >;

    // A callback that is called for each incoming Feature message, and finally with a status.
    using list_features_cb_t = std::function<void(feature_or_status_t)>;

    void listFeatures(::routeguide::Rectangle& rect, list_features_cb_t&& fn) {
        class Impl
            : Base
            , grpc::ClientReadReactor<::routeguide::Feature> {
        public:

            Impl(EverythingCallbackClient& owner,
                 ::routeguide::Rectangle& rect,
                 list_features_cb_t && fn)
                : Base(owner), req_{std::move(rect)}, caller_callback_{std::move(fn)} {

                LOG_TRACE << "listFeatures starting async request.";

                owner_.stub_->async()->ListFeatures(&ctx_, &req_, this);

                StartRead(&resp_);

                // Here we will initiate the actual async RPC
                StartCall();
            }

            void OnReadDone(bool ok) override {

                // We go on reading while ok
                if (ok) {
                    LOG_TRACE << "Request successful. Message: " << resp_.name();

                    caller_callback_(&resp_);
                    resp_.Clear();

                    StartRead(&resp_);
                } else {
                    LOG_TRACE << "Read failed (end of stream?)";
                }
            }
            void OnDone(const grpc::Status& s) override {
                if (s.ok()) {
                    LOG_TRACE << "Request succeeded.";
                } else {
                    LOG_TRACE << "Request failed: " << s.error_message();
                }

                caller_callback_(s);
                delete this;
            }

        private:

            grpc::ClientContext ctx_;
            ::routeguide::Rectangle req_;
            ::routeguide::Feature resp_;
            list_features_cb_t caller_callback_;
        };

        // Shorthand for `new Impl(...)` but with exception guarantee.
        std::make_unique<Impl>(*this, rect, std::move(fn)).release();
    }

    void nextGextFeature(size_t recid) {
        // Initiate a new request
        ::routeguide::Point point;
        point.set_latitude(recid);
        point.set_longitude(100);

        LOG_TRACE << "Calling getFeature #" << recid;

        getFeature(point, [this, recid](const grpc::Status& status,
                                 const ::routeguide::Feature& feature) {
            if (status.ok()) {
                LOG_TRACE << "#" << recid << " received feature: "
                          << feature.name();

                // Initiate the next request
                nextRequest();
            } else {
                LOG_TRACE << "#" << recid << " failed: "
                          << status.error_message();
            }
        });
    }

    void nextListFeatures(size_t recid) {
        ::routeguide::Rectangle rect;
        rect.mutable_hi()->set_latitude(recid);
        rect.mutable_hi()->set_longitude(2);

        LOG_TRACE << "Calling listFeatures #" << recid;

        listFeatures(rect, [this, recid](feature_or_status_t val) {

            if (std::holds_alternative<const ::routeguide::Feature *>(val)) {
                auto feature = std::get<const ::routeguide::Feature *>(val);
                assert(feature);
                LOG_TRACE << "nextListFeatures #" << recid
                          << " - Received feature: " << feature->name();
            } else if (std::holds_alternative<grpc::Status>(val)) {
                auto status = std::get<grpc::Status>(val);
                if (status.ok()) {
                    LOG_TRACE << "nextListFeatures #" << recid
                              << " done. Initiating next request ...";
                    nextRequest();
                } else {
                    LOG_TRACE << "nextListFeatures #" << recid
                              << " failed: " <<  status.error_message();
                }
            } else {
                assert(false && "unexpected value type in variant!");
            }

        });
    }

    void nextRequest() {
        static const std::array<std::function<void(size_t)>, 4> request_variants = {
            [this](size_t recid){nextGextFeature(recid);},
            [this](size_t recid){nextListFeatures(recid);},
//            [this]{createNext<RecordRouteRequest>();},
//            [this]{createNext<RouteChatRequest>();},
            };

        if (auto recid = ++request_count_; recid <= config_.num_requests) {
            request_variants.at(config_.request_type)(recid);
        }
    }


    void run() {

        // Start the first request(s)
        for(auto i = 0; i < config_.parallel_requests; ++i) {
            nextRequest();
        }

        // Wait for everything to finish
        LOG_DEBUG << "Waiting for all requests to finish...";
        done_.get_future().get();
        LOG_INFO << "Done!";
    }

private:
    std::atomic_size_t request_count_{0};
    std::atomic_size_t in_flight_{0};

    const Config config_;

    // This is a connection to the gRPC server
    std::shared_ptr<grpc::Channel> channel_;

     // An instance of the client that was generated from our .proto file.
    std::unique_ptr<::routeguide::RouteGuide::Stub> stub_;

    std::promise<void> done_;
};
