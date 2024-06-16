# Fun with gRPC and C++

In 2023 I wanted to use gRPC in my upcoming [DNS server](https://github.com/jgaa/nsblast).

I have used gRPC in the past - with great pain. This time around I looked at some examples and made 
kind of an implementation - but I realized it was crap. To add injury to insult, 
there were simply too many things I did not know or understand properly to fix it. 
So I decided to spend some time to play with gRPC to get a better understanding.

The code in this repository is primarily intended as examples used in a blog-series about 
gRPC and C++.

Blog: [Fun with gRPC and C++](https://lastviking.eu/fun_with_gRPC_and_C++/)

In June 2024, the QT Company released QT 6.8 beta, with native support for gRPC.
I have updated this repository with an example app showing how to implement the
same "Route Guide" API that the other examples use, but with QT's own gRPC code
generator. The entire project use CMake.

For a more extensive example of how I use gRPC now, both in clean C++ code (server)
and in a QT Desktop/Mobile application, you can look at [Next-app](https://github.com/jgaa/next-app).
