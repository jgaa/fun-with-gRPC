#pragma once

#include <string>
#include <cstddef>

struct Config {
    size_t num_stream_messages = 16;
    size_t num_requests = 1; // for clients
    size_t parallel_requests = 1;
    std::string address = "127.0.0.1:10123";
    bool do_push_back_on_queue = false;

    // For the clients
    enum RequestType : int {
        GetFeature = 0,
        ListFeatures = 1
    } request_type = GetFeature;
};
