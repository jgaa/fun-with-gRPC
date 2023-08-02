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

        new Impl(*this, point, std::move(fn));
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

        new Impl(*this, rect, std::move(fn));
    }

    using on_ready_to_write_point_cb_t = std::function<bool(::routeguide::Point& point)>;
    using on_done_route_summary_cb_t = std::function<
        void(const grpc::Status& status, ::routeguide::RouteSummary&)>;

    void recordRoute(on_ready_to_write_point_cb_t&& writerCb, on_done_route_summary_cb_t&& doneCb) {

        class Impl
            : Base
            , grpc::ClientWriteReactor<::routeguide::Point> {
        public:
            Impl(EverythingCallbackClient& owner,
                 on_ready_to_write_point_cb_t& writerCb,
                 on_done_route_summary_cb_t& doneCb)
                : Base(owner), writer_cb_{std::move(writerCb)}
                , done_cb_{std::move(doneCb)}
            {
                LOG_TRACE << "recordRoute starting async request.";

                owner_.stub_->async()->RecordRoute(&ctx_, &resp_, this);

                write();

                // Here we will initiate the actual async RPC
                StartCall();
            }

            void OnWriteDone(bool ok) override {
                write();
            }

            void OnDone(const grpc::Status& s) override {
                done_cb_(s, resp_);
                delete this;
            }

        private:
            void write() {
                // Get another message from the caller
                req_.Clear();
                if (writer_cb_(req_)) {

                    // Note that if we had implemented a delayed `StartWrite()`, for example
                    // by relaying the event to a task manager like `boost::asio::io_service::post()`,
                    // we would need to call `AddHold()` either here or in the constructor.
                    // Only when we had called `RemoveHold()` the same number of times, would
                    // gRPC consider calling the `OnDone()` event method.

                    StartWrite(&req_);
                } else {
                    StartWritesDone();
                }
            }

            grpc::ClientContext ctx_;
            ::routeguide::Point req_;
            ::routeguide::RouteSummary resp_;
            on_ready_to_write_point_cb_t writer_cb_;
            on_done_route_summary_cb_t done_cb_;
        };

        new Impl(*this, writerCb, doneCb);
    }

    using on_say_something_cb_t = std::function<bool(::routeguide::RouteNote&)>;
    using on_got_message_cb_t = std::function<void(::routeguide::RouteNote&)>;
    using on_done_status_cb_t = std::function<void(const grpc::Status&)>;


    void routeChat(on_say_something_cb_t&& outgoing,
                   on_got_message_cb_t&& incoming,
                   on_done_status_cb_t&& done) {

        class Impl
            : Base
            , grpc::ClientBidiReactor<::routeguide::RouteNote,
                                      ::routeguide::RouteNote>
        {
        public:
            Impl(EverythingCallbackClient& owner,
                 on_say_something_cb_t& outgoing,
                 on_got_message_cb_t& incoming,
                 on_done_status_cb_t& done)
                : Base(owner), outgoing_{std::move(outgoing)}
                , incoming_{std::move(incoming)}
                , done_{std::move(done)} {

                LOG_TRACE << "routeChat starting async request.";

                owner_.stub_->async()->RouteChat(&ctx_, this);

                read();
                write();

                // Here we will initiate the actual async RPC
                StartCall();

            }

            void OnWriteDone(bool ok) override {
                write();
            }
            void OnReadDone(bool ok) override {
                if (ok) {
                    incoming_(in_);
                    read();
                }
            }
            void OnDone(const grpc::Status& s) override {
                done_(s);
                delete this;
            }

        private:
            void read() {
                in_.Clear();
                StartRead(&in_);
            }

            void write() {
                out_.Clear();
                if (outgoing_(out_)) {
                    StartWrite(&out_);
                    return;
                }

                StartWritesDone();
            }

            grpc::ClientContext ctx_;
            ::routeguide::RouteNote in_;
            ::routeguide::RouteNote out_;
            on_say_something_cb_t outgoing_;
            on_got_message_cb_t incoming_;
            on_done_status_cb_t done_;
        };

        new Impl(*this, outgoing, incoming, done);
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

    void nextRecordRoute(size_t recid) {

        recordRoute(
            // Callback to provide data to send to the server
            // Note that we instatiate a local variable `count` that lives
            // in the scope of one instance of the lambda function.
            [this, recid, count=size_t{0}](::routeguide::Point& point) mutable {
                if (++count > config_.num_stream_messages) [[unlikely]] {
                    // We are done
                    return false;
                }

                // Just pick some data to set.
                // In a real implementation we would have to get prerpared data or
                // data from a quick calculation. We have to return immediately since
                // we are using one of gRPC's worker threads.
                // If we needed to do some work, like fetching from a database, we
                // would need another workflow where the event was dispatched to a
                // task manager or thread-pool, argument was a write functor rather than
                // the data object itself.
                point.set_latitude(count);
                point.set_longitude(100);

                LOG_TRACE << "RecordRoute reuest# " << recid
                          << " - sending latitude " << count;

                return true;
            },

            // Callback to handle the completion of the request and its status/reply.
            [this, recid](const grpc::Status& status, ::routeguide::RouteSummary& summery) mutable {
                if (!status.ok()) {
                    LOG_WARN << "RecordRoute reuest # " << recid
                             << " failed: " << status.error_message();
                    return;
                }

                LOG_TRACE << "RecordRoute request #" << recid << " is done. Distance: "
                          << summery.distance();

                nextRequest();
            });
    }


    void nextRouteChat(size_t recid) {

        routeChat(
            // Compose an outgoing message
            [this, recid, count=size_t{0}](::routeguide::RouteNote& msg) mutable {
                if (++count > config_.num_stream_messages) [[unlikely]] {
                    // We are done
                    return false;
                }

                // Just pick some data to set.
                msg.set_message(std::string{"chat message "} + std::to_string(count));

                LOG_TRACE << "RouteChat reuest# " << recid
                          << " outgoing message " << count;

                return true;
            },
            // We received an incoming message
            [this, recid ](::routeguide::RouteNote& msg) {
                LOG_TRACE << "RouteChat reuest# " << recid
                          << " incoming message: " << msg.message();
            },
            // The conversation is over.
            [this, recid ](const grpc::Status& status) {
                if (!status.ok()) {
                    LOG_WARN << "RouteChat reuest # " << recid
                             << " failed: " << status.error_message();
                    return;
                }

                LOG_TRACE << "RecordRoute request #" << recid << " is done.";
                nextRequest();
            }

            );

    }

    void nextRequest() {
        static const std::array<std::function<void(size_t)>, 4> request_variants = {
            [this](size_t recid){nextGextFeature(recid);},
            [this](size_t recid){nextListFeatures(recid);},
            [this](size_t recid){nextRecordRoute(recid);},
            [this](size_t recid){nextRouteChat(recid);},
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
