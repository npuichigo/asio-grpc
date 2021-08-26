// Copyright 2021 Dennis Hezel
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "protos/helloworld.grpc.pb.h"

#include <agrpc/asioGrpc.hpp>
#include <boost/asio/spawn.hpp>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>

void send_initial_metadata(grpc::ServerAsyncResponseWriter<helloworld::HelloReply>& writer,
                           boost::asio::yield_context& yield)
{
    // begin-snippet: send_initial_metadata-server-side
    bool send_ok = agrpc::send_initial_metadata(writer, yield);
    // end-snippet
}

void finish_with_error(grpc::ServerAsyncResponseWriter<helloworld::HelloReply>& writer,
                       boost::asio::yield_context& yield)
{
    // begin-snippet: finish_with_error-server-side
    bool finish_ok = agrpc::finish_with_error(writer, grpc::Status::CANCELLED, yield);
    // end-snippet
}

int main()
{
    std::unique_ptr<grpc::Server> server;
    helloworld::Greeter::AsyncService service;

    // begin-snippet: create-grpc_context-server-side
    grpc::ServerBuilder builder;
    agrpc::GrpcContext grpc_context{builder.AddCompletionQueue()};
    // end-snippet

    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    server = builder.BuildAndStart();

    auto guard = boost::asio::make_work_guard(grpc_context);
    boost::asio::spawn(grpc_context,
                       [&](auto yield)
                       {
                           // begin-snippet: request-unary-server-side
                           grpc::ServerContext server_context;
                           helloworld::HelloRequest request;
                           grpc::ServerAsyncResponseWriter<helloworld::HelloReply> writer{&server_context};
                           bool request_ok = agrpc::request(&helloworld::Greeter::AsyncService::RequestSayHello,
                                                            service, server_context, request, writer, yield);
                           // end-snippet
                           // begin-snippet: finish-server-side
                           helloworld::HelloReply response;
                           std::string prefix("Hello ");
                           response.set_message(prefix + request.name());
                           bool finish_ok = agrpc::finish(writer, response, grpc::Status::OK, yield);
                           // end-snippet
                       });

    // begin-snippet: run-grpc_context-server-side
    grpc_context.run();
    server->Shutdown();
}  // grpc_context is destructed here
   // end-snippet