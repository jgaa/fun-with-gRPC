#pragma once

#include <cstddef>

struct  Config {
    size_t num_requests = 1;
    size_t parallel_requests = 1;
    size_t stream_messages = 3;
    bool do_push_back_on_queue = true;

    enum RequestType : int {
        GetFeature = 0,
        ListFeatures = 1
    } request_type = GetFeature;
};
