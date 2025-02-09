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

#include "agrpc/asioGrpc.hpp"
#include "protos/test.grpc.pb.h"
#include "utils/asioUtils.hpp"
#include "utils/grpcClientServerTest.hpp"
#include "utils/grpcContextTest.hpp"

#include <doctest/doctest.h>
#include <grpcpp/alarm.h>

#include <cstddef>
#include <optional>
#include <string_view>
#include <thread>

namespace test_asio_grpc
{
using namespace agrpc;

TEST_SUITE_BEGIN(ASIO_GRPC_TEST_CPP_VERSION* doctest::timeout(180.0));

TEST_CASE("GrpcExecutor fulfills Executor TS traits")
{
    using Exec = agrpc::GrpcContext::executor_type;
    CHECK(asio::execution::can_execute_v<std::add_const_t<Exec>, asio::execution::invocable_archetype>);
    CHECK(asio::execution::is_executor_v<Exec>);
    CHECK(asio::can_require_v<Exec, asio::execution::blocking_t::never_t>);
    CHECK(asio::can_prefer_v<Exec, asio::execution::blocking_t::possibly_t>);
    CHECK(asio::can_prefer_v<Exec, asio::execution::relationship_t::fork_t>);
    CHECK(asio::can_prefer_v<Exec, asio::execution::relationship_t::continuation_t>);
    CHECK(asio::can_prefer_v<Exec, asio::execution::outstanding_work_t::tracked_t>);
    CHECK(asio::can_prefer_v<Exec, asio::execution::outstanding_work_t::untracked_t>);
    CHECK(asio::can_prefer_v<Exec, asio::execution::allocator_t<agrpc::detail::pmr::polymorphic_allocator<std::byte>>>);
    CHECK(asio::can_query_v<Exec, asio::execution::blocking_t>);
    CHECK(asio::can_query_v<Exec, asio::execution::relationship_t>);
    CHECK(asio::can_query_v<Exec, asio::execution::outstanding_work_t>);
    CHECK(asio::can_query_v<Exec, asio::execution::mapping_t>);
    CHECK(asio::can_query_v<Exec, asio::execution::allocator_t<void>>);
    CHECK(asio::can_query_v<Exec, asio::execution::context_t>);
    CHECK(std::is_constructible_v<asio::any_io_executor, Exec>);
    agrpc::GrpcContext grpc_context{std::make_unique<grpc::CompletionQueue>()};
    auto executor = grpc_context.get_executor();
    CHECK_EQ(asio::execution::blocking_t::possibly,
             asio::query(asio::require(executor, asio::execution::blocking_t::possibly), asio::execution::blocking));
    CHECK_EQ(asio::execution::blocking_t::never,
             asio::query(asio::require(executor, asio::execution::blocking_t::never), asio::execution::blocking));
    CHECK_EQ(asio::execution::relationship_t::continuation,
             asio::query(asio::prefer(executor, asio::execution::relationship_t::continuation),
                         asio::execution::relationship));
    CHECK_EQ(asio::execution::relationship_t::fork,
             asio::query(asio::prefer(executor, asio::execution::relationship_t::fork), asio::execution::relationship));
    CHECK_EQ(asio::execution::outstanding_work_t::tracked,
             asio::query(asio::prefer(executor, asio::execution::outstanding_work_t::tracked),
                         asio::execution::outstanding_work));
    CHECK_EQ(asio::execution::outstanding_work_t::untracked,
             asio::query(asio::prefer(executor, asio::execution::outstanding_work_t::untracked),
                         asio::execution::outstanding_work));
}

TEST_CASE("GrpcExecutor is mostly trivial")
{
    CHECK(std::is_trivially_copy_constructible_v<agrpc::GrpcExecutor>);
    CHECK(std::is_trivially_move_constructible_v<agrpc::GrpcExecutor>);
    CHECK(std::is_trivially_destructible_v<agrpc::GrpcExecutor>);
    CHECK(std::is_trivially_copy_assignable_v<agrpc::GrpcExecutor>);
    CHECK(std::is_trivially_move_assignable_v<agrpc::GrpcExecutor>);
    CHECK_EQ(sizeof(void*), sizeof(agrpc::GrpcExecutor));
}

TEST_CASE("Work tracking GrpcExecutor constructor and assignment")
{
    agrpc::GrpcContext grpc_context{std::make_unique<grpc::CompletionQueue>()};
    agrpc::GrpcContext other_context{std::make_unique<grpc::CompletionQueue>()};

    auto other_ex = asio::prefer(grpc_context.get_executor(), asio::execution::blocking_t::possibly);
    auto ex = asio::require(grpc_context.get_executor(), asio::execution::outstanding_work_t::tracked,
                            asio::execution::allocator(agrpc::detail::pmr::polymorphic_allocator<std::byte>()));
    auto ex2a = asio::require(ex, asio::execution::allocator);
    CHECK_EQ(std::allocator<void>{}, asio::query(ex2a, asio::execution::allocator));
    CHECK_NE(other_ex, ex2a);
    CHECK_NE(grpc_context.get_executor(), ex2a);
    CHECK_NE(other_context.get_executor(), ex2a);

    const auto ex1{ex};
    auto ex2{ex};
    auto ex3{std::move(ex)};
    CHECK_EQ(ex3, ex2);
    ex2 = ex1;
    CHECK_EQ(ex2, ex1);
    ex2 = std::move(ex3);
    ex2 = std::move(ex2);
    CHECK_EQ(ex2, ex1);
    ex3 = std::move(ex2);
    CHECK_EQ(ex3, ex1);
    ex2 = std::as_const(ex3);
    ex2 = std::as_const(ex2);
    CHECK_EQ(ex2, ex1);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "GrpcContext::reset")
{
    bool ok = false;
    CHECK_FALSE(grpc_context.is_stopped());
    asio::post(grpc_context,
               [&]
               {
                   ok = true;
                   CHECK_FALSE(grpc_context.is_stopped());
               });
    grpc_context.run();
    CHECK(grpc_context.is_stopped());
    CHECK(ok);
    asio::post(grpc_context,
               [&]
               {
                   ok = false;
               });
    grpc_context.run();
    CHECK(ok);
    grpc_context.reset();
    asio::post(grpc_context,
               [&]
               {
                   ok = false;
               });
    grpc_context.run();
    CHECK_FALSE(ok);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "GrpcContext::stop does not complete pending operations")
{
    bool ok = false;
    asio::post(grpc_context,
               [&]
               {
                   grpc_context.stop();
                   asio::post(grpc_context,
                              [&]
                              {
                                  ok = true;
                              });
               });
    grpc_context.run();
    CHECK_FALSE(ok);
}

TEST_CASE("GrpcContext::stop while waiting for Alarm will not invoke the Alarm's completion handler")
{
    bool is_stop_from_same_thread = true;
    SUBCASE("stop from same thread") {}
    SUBCASE("stop from other thread") { is_stop_from_same_thread = false; }
    bool ok = false;
    {
        std::thread thread;
        agrpc::GrpcContext grpc_context{std::make_unique<grpc::CompletionQueue>()};
        auto guard = asio::make_work_guard(grpc_context);
        grpc::Alarm alarm;
        asio::post(grpc_context,
                   [&]
                   {
                       agrpc::wait(alarm, std::chrono::system_clock::now() + std::chrono::seconds(5),
                                   asio::bind_executor(grpc_context,
                                                       [&](bool)
                                                       {
                                                           ok = true;
                                                       }));
                       if (is_stop_from_same_thread)
                       {
                           grpc_context.stop();
                           guard.reset();
                       }
                       else
                       {
                           thread = std::thread(
                               [&]
                               {
                                   grpc_context.stop();
                                   guard.reset();
                               });
                       }
                   });
        grpc_context.run();
        CHECK_FALSE(ok);
        if (!is_stop_from_same_thread)
        {
            thread.join();
        }
    }
    CHECK_FALSE(ok);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "asio::spawn an Alarm and yield its wait")
{
    bool ok = false;
    std::chrono::system_clock::time_point start;
    asio::spawn(asio::bind_executor(get_executor(), [] {}),
                [&](auto&& yield)
                {
                    grpc::Alarm alarm;
                    start = std::chrono::system_clock::now();
                    ok = agrpc::wait(alarm, test::hundred_milliseconds_from_now(), yield);
                });
    grpc_context.run();
    CHECK_LE(std::chrono::milliseconds(100), std::chrono::system_clock::now() - start);
    CHECK(ok);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "asio::post an Alarm and check time")
{
    bool ok = false;
    std::chrono::system_clock::time_point start;
    grpc::Alarm alarm;
    asio::post(grpc_context,
               [&]()
               {
                   start = std::chrono::system_clock::now();
                   agrpc::wait(alarm, test::hundred_milliseconds_from_now(),
                               asio::bind_executor(grpc_context,
                                                   [&](bool)
                                                   {
                                                       ok = true;
                                                   }));
               });
    grpc_context.run();
    CHECK_LE(std::chrono::milliseconds(100), std::chrono::system_clock::now() - start);
    CHECK(ok);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "asio::post a asio::steady_timer")
{
    std::optional<test::ErrorCode> error_code;
    asio::steady_timer timer{get_executor()};
    asio::post(get_executor(),
               [&]
               {
                   timer.expires_after(std::chrono::milliseconds(10));
                   timer.async_wait(
                       [&](const test::ErrorCode& ec)
                       {
                           error_code.emplace(ec);
                       });
               });
    grpc_context.run();
    CHECK_EQ(test::ErrorCode{}, error_code);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "asio::spawn with yield_context")
{
    bool ok = false;
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    grpc::Alarm alarm;
                    ok = agrpc::wait(alarm, test::ten_milliseconds_from_now(), yield);
                });
    grpc_context.run();
    CHECK(ok);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "post from multiple threads")
{
    static constexpr auto THREAD_COUNT = 32;
    std::atomic_int counter{};
    asio::thread_pool pool{THREAD_COUNT};
    auto guard = asio::make_work_guard(grpc_context);
    for (size_t i = 0; i < THREAD_COUNT; ++i)
    {
        asio::post(pool,
                   [&]
                   {
                       asio::post(grpc_context,
                                  [&]
                                  {
                                      if (++counter == THREAD_COUNT)
                                      {
                                          guard.reset();
                                      }
                                  });
                   });
    }
    asio::post(pool,
               [&]
               {
                   grpc_context.run();
               });
    pool.join();
    CHECK_EQ(THREAD_COUNT, counter);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "post/execute with allocator")
{
    SUBCASE("asio::post")
    {
        asio::post(grpc_context,
                   test::HandlerWithAssociatedAllocator{
                       [] {}, agrpc::detail::pmr::polymorphic_allocator<std::byte>(&resource)});
    }
    SUBCASE("asio::execute before grpc_context.run()")
    {
        get_pmr_executor().execute([] {});
    }
    SUBCASE("asio::execute after grpc_context.run() from same thread")
    {
        asio::post(grpc_context,
                   [&, exec = get_pmr_executor()]
                   {
                       exec.execute([] {});
                   });
    }
    SUBCASE("agrpc::wait")
    {
        asio::execution::execute(get_executor(),
                                 [&, executor = get_pmr_executor()]() mutable
                                 {
                                     auto alarm = std::make_shared<grpc::Alarm>();
                                     auto& alarm_ref = *alarm;
                                     agrpc::wait(alarm_ref, test::ten_milliseconds_from_now(),
                                                 asio::bind_executor(std::move(executor),
                                                                     [a = std::move(alarm)](bool ok)
                                                                     {
                                                                         CHECK(ok);
                                                                     }));
                                 });
    }
    grpc_context.run();
    CHECK(std::any_of(buffer.begin(), buffer.end(),
                      [](auto&& value)
                      {
                          return value != std::byte{};
                      }));
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "dispatch with allocator")
{
    asio::post(grpc_context,
               [&]
               {
                   asio::dispatch(get_pmr_executor(), [] {});
               });
    grpc_context.run();
    CHECK(std::all_of(buffer.begin(), buffer.end(),
                      [](auto&& value)
                      {
                          return value == std::byte{};
                      }));
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "execute with throwing allocator")
{
    const auto executor =
        asio::require(get_executor(), asio::execution::allocator(agrpc::detail::pmr::polymorphic_allocator<std::byte>(
                                          agrpc::detail::pmr::null_memory_resource())));
    CHECK_THROWS(asio::execution::execute(executor, [] {}));
}

struct Exception : std::exception
{
};

TEST_CASE_FIXTURE(test::GrpcContextTest, "asio::post with throwing completion handler")
{
    asio::post(get_executor(), asio::bind_executor(get_executor(),
                                                   []
                                                   {
                                                       throw Exception{};
                                                   }));
    CHECK_THROWS_AS(grpc_context.run(), Exception);
}

template <class Function>
struct Coro : asio::coroutine
{
    using executor_type =
        asio::require_result<agrpc::GrpcContext::executor_type, asio::execution::outstanding_work_t::tracked_t>::type;

    executor_type executor;
    Function function;

    Coro(agrpc::GrpcContext& grpc_context, Function&& f)
        : executor(asio::require(grpc_context.get_executor(), asio::execution::outstanding_work_t::tracked)),
          function(std::forward<Function>(f))
    {
    }

    void operator()(bool ok) { function(ok, this); }

    executor_type get_executor() const noexcept { return executor; }
};

#ifdef AGRPC_STANDALONE_ASIO
#include <asio/yield.hpp>
#elif defined(AGRPC_BOOST_ASIO)
#include <boost/asio/yield.hpp>
#endif

TEST_CASE_FIXTURE(test::GrpcClientServerTest, "unary stackless coroutine")
{
    grpc::ServerAsyncResponseWriter<test::v1::Response> writer{&server_context};
    test::v1::Request server_request;
    test::v1::Response server_response;
    auto server_loop = [&](bool ok, auto* coro) mutable
    {
        reenter(*coro)
        {
            yield agrpc::request(&test::v1::Test::AsyncService::RequestUnary, service, server_context, server_request,
                                 writer, *coro);
            CHECK(ok);
            CHECK_EQ(42, server_request.integer());
            server_response.set_integer(21);
            yield agrpc::finish(writer, server_response, grpc::Status::OK, *coro);
            CHECK(ok);
        }
    };
    std::thread server_thread(
        [&, server_coro = Coro{grpc_context, std::move(server_loop)}]() mutable
        {
            server_coro(true);
        });

    test::v1::Request client_request;
    client_request.set_integer(42);
    test::v1::Response client_response;
    grpc::Status status;
    std::unique_ptr<grpc::ClientAsyncResponseReader<test::v1::Response>> reader;
    auto client_loop = [&](bool ok, auto* coro) mutable
    {
        reenter(*coro)
        {
            reader = stub->AsyncUnary(&client_context, client_request, agrpc::get_completion_queue(*coro));
            yield agrpc::finish(*reader, client_response, status, *coro);
            CHECK(ok);
            CHECK(status.ok());
            CHECK_EQ(21, client_response.integer());
        }
    };
    std::thread client_thread(
        [&, client_coro = Coro{grpc_context, std::move(client_loop)}]() mutable
        {
            client_coro(true);
        });

    grpc_context.run();
    server_thread.join();
    client_thread.join();
}

#ifdef AGRPC_STANDALONE_ASIO
#include <asio/unyield.hpp>
#elif defined(AGRPC_BOOST_ASIO)
#include <boost/asio/unyield.hpp>
#endif

TEST_CASE_FIXTURE(test::GrpcClientServerTest, "yield_context server streaming")
{
    bool use_write_and_finish{false};
    SUBCASE("server write_and_finish") { use_write_and_finish = true; }
    bool use_client_convenience{false};
    SUBCASE("client use convenience") { use_client_convenience = true; }
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    test::v1::Request request;
                    grpc::ServerAsyncWriter<test::v1::Response> writer{&server_context};
                    CHECK(agrpc::request(&test::v1::Test::AsyncService::RequestServerStreaming, service, server_context,
                                         request, writer, yield));
                    CHECK(agrpc::send_initial_metadata(writer, yield));
                    CHECK_EQ(42, request.integer());
                    test::v1::Response response;
                    response.set_integer(21);
                    if (use_write_and_finish)
                    {
                        CHECK(agrpc::write_and_finish(writer, response, {}, grpc::Status::OK, yield));
                    }
                    else
                    {
                        CHECK(agrpc::write(writer, response, yield));
                        CHECK(agrpc::finish(writer, grpc::Status::OK, yield));
                    }
                });
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    test::v1::Request request;
                    request.set_integer(42);
                    auto [reader, ok] = [&]
                    {
                        if (use_client_convenience)
                        {
                            return agrpc::request(&test::v1::Test::Stub::AsyncServerStreaming, *stub, client_context,
                                                  request, yield);
                        }
                        std::unique_ptr<grpc::ClientAsyncReader<test::v1::Response>> reader;
                        bool ok = agrpc::request(&test::v1::Test::Stub::AsyncServerStreaming, *stub, client_context,
                                                 request, reader, yield);
                        return std::pair{std::move(reader), ok};
                    }();
                    CHECK(ok);
                    CHECK(agrpc::read_initial_metadata(*reader, yield));
                    test::v1::Response response;
                    CHECK(agrpc::read(*reader, response, yield));
                    grpc::Status status;
                    CHECK(agrpc::finish(*reader, status, yield));
                    CHECK(status.ok());
                    CHECK_EQ(21, response.integer());
                });
    grpc_context.run();
}

TEST_CASE_FIXTURE(test::GrpcClientServerTest, "yield_context client streaming")
{
    bool use_client_convenience{};
    SUBCASE("client use convenience") { use_client_convenience = true; }
    SUBCASE("client do not use convenience") { use_client_convenience = false; }
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    grpc::ServerAsyncReader<test::v1::Response, test::v1::Request> reader{&server_context};
                    CHECK(agrpc::request(&test::v1::Test::AsyncService::RequestClientStreaming, service, server_context,
                                         reader, yield));
                    CHECK(agrpc::send_initial_metadata(reader, yield));
                    test::v1::Request request;
                    CHECK(agrpc::read(reader, request, yield));
                    CHECK_EQ(42, request.integer());
                    test::v1::Response response;
                    response.set_integer(21);
                    CHECK(agrpc::finish(reader, response, grpc::Status::OK, yield));
                });
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    test::v1::Response response;
                    auto [writer, ok] = [&]
                    {
                        if (use_client_convenience)
                        {
                            return agrpc::request(&test::v1::Test::Stub::AsyncClientStreaming, *stub, client_context,
                                                  response, yield);
                        }
                        std::unique_ptr<grpc::ClientAsyncWriter<test::v1::Request>> writer;
                        bool ok = agrpc::request(&test::v1::Test::Stub::AsyncClientStreaming, *stub, client_context,
                                                 writer, response, yield);
                        return std::pair{std::move(writer), ok};
                    }();
                    CHECK(ok);
                    CHECK(agrpc::read_initial_metadata(*writer, yield));
                    test::v1::Request request;
                    request.set_integer(42);
                    CHECK(agrpc::write(*writer, request, yield));
                    CHECK(agrpc::writes_done(*writer, yield));
                    grpc::Status status;
                    CHECK(agrpc::finish(*writer, status, yield));
                    CHECK(status.ok());
                    CHECK_EQ(21, response.integer());
                });
    grpc_context.run();
}

TEST_CASE_FIXTURE(test::GrpcClientServerTest, "yield_context unary")
{
    bool use_finish_with_error;
    SUBCASE("server finish_with_error") { use_finish_with_error = true; }
    SUBCASE("server finish with OK") { use_finish_with_error = false; }
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    test::v1::Request request;
                    grpc::ServerAsyncResponseWriter<test::v1::Response> writer{&server_context};
                    CHECK(agrpc::request(&test::v1::Test::AsyncService::RequestUnary, service, server_context, request,
                                         writer, yield));
                    CHECK(agrpc::send_initial_metadata(writer, yield));
                    CHECK_EQ(42, request.integer());
                    test::v1::Response response;
                    response.set_integer(21);
                    if (use_finish_with_error)
                    {
                        CHECK(agrpc::finish_with_error(writer, grpc::Status::CANCELLED, yield));
                    }
                    else
                    {
                        CHECK(agrpc::finish(writer, response, grpc::Status::OK, yield));
                    }
                });
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    test::v1::Request request;
                    request.set_integer(42);
                    auto reader =
                        stub->AsyncUnary(&client_context, request, agrpc::get_completion_queue(get_executor()));
                    CHECK(agrpc::read_initial_metadata(*reader, yield));
                    test::v1::Response response;
                    grpc::Status status;
                    CHECK(agrpc::finish(*reader, response, status, yield));
                    if (use_finish_with_error)
                    {
                        CHECK_EQ(grpc::StatusCode::CANCELLED, status.error_code());
                    }
                    else
                    {
                        CHECK(status.ok());
                        CHECK_EQ(21, response.integer());
                    }
                });
    grpc_context.run();
}

TEST_CASE_FIXTURE(test::GrpcClientServerTest, "yield_context bidirectional streaming")
{
    bool use_write_and_finish{false};
    SUBCASE("server write_and_finish") { use_write_and_finish = true; }
    bool use_client_convenience{false};
    SUBCASE("client use convenience") { use_client_convenience = true; }
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    grpc::ServerAsyncReaderWriter<test::v1::Response, test::v1::Request> reader_writer{&server_context};
                    CHECK(agrpc::request(&test::v1::Test::AsyncService::RequestBidirectionalStreaming, service,
                                         server_context, reader_writer, yield));
                    CHECK(agrpc::send_initial_metadata(reader_writer, yield));
                    test::v1::Request request;
                    CHECK(agrpc::read(reader_writer, request, yield));
                    CHECK_EQ(42, request.integer());
                    test::v1::Response response;
                    response.set_integer(21);
                    if (use_write_and_finish)
                    {
                        CHECK(agrpc::write_and_finish(reader_writer, response, {}, grpc::Status::OK, yield));
                    }
                    else
                    {
                        CHECK(agrpc::write(reader_writer, response, yield));
                        CHECK(agrpc::finish(reader_writer, grpc::Status::OK, yield));
                    }
                });
    asio::spawn(
        get_executor(),
        [&](asio::yield_context yield)
        {
            auto [reader_writer, ok] = [&]
            {
                if (use_client_convenience)
                {
                    return agrpc::request(&test::v1::Test::Stub::AsyncBidirectionalStreaming, *stub, client_context,
                                          yield);
                }
                std::unique_ptr<grpc::ClientAsyncReaderWriter<test::v1::Request, test::v1::Response>> reader_writer;
                bool ok = agrpc::request(&test::v1::Test::Stub::AsyncBidirectionalStreaming, *stub, client_context,
                                         reader_writer, yield);
                return std::pair{std::move(reader_writer), ok};
            }();
            CHECK(ok);
            CHECK(agrpc::read_initial_metadata(*reader_writer, yield));
            test::v1::Request request;
            request.set_integer(42);
            CHECK(agrpc::write(*reader_writer, request, yield));
            CHECK(agrpc::writes_done(*reader_writer, yield));
            test::v1::Response response;
            CHECK(agrpc::read(*reader_writer, response, yield));
            grpc::Status status;
            CHECK(agrpc::finish(*reader_writer, status, yield));
            CHECK(status.ok());
            CHECK_EQ(21, response.integer());
        });
    grpc_context.run();
}

struct GrpcRepeatedlyRequestTest : test::GrpcClientServerTest
{
    template <class RPC, class Service, class ServerFunction, class ClientFunction>
    auto test(RPC rpc, Service& service, ServerFunction server_function, ClientFunction client_function)
    {
        agrpc::repeatedly_request(
            rpc, service, test::RpcSpawner{asio::bind_executor(this->get_executor(), std::move(server_function))});
        asio::spawn(get_executor(), std::move(client_function));
    }
};

TEST_CASE_FIXTURE(GrpcRepeatedlyRequestTest, "yield_context repeatedly_request unary")
{
    bool is_shutdown{false};
    auto request_count{0};
    this->test(
        &test::v1::Test::AsyncService::RequestUnary, service,
        [&](grpc::ServerContext&, test::v1::Request& request,
            grpc::ServerAsyncResponseWriter<test::v1::Response> writer, asio::yield_context yield)
        {
            CHECK_EQ(42, request.integer());
            test::v1::Response response;
            response.set_integer(21);
            ++request_count;
            if (request_count > 3)
            {
                is_shutdown = true;
            }
            CHECK(agrpc::finish(writer, response, grpc::Status::OK, yield));
        },
        [&](asio::yield_context yield)
        {
            while (!is_shutdown)
            {
                test::v1::Request request;
                request.set_integer(42);
                grpc::ClientContext new_client_context;
                auto reader =
                    stub->AsyncUnary(&new_client_context, request, agrpc::get_completion_queue(get_executor()));
                test::v1::Response response;
                grpc::Status status;
                CHECK(agrpc::finish(*reader, response, status, yield));
                CHECK(status.ok());
                CHECK_EQ(21, response.integer());
            }
            grpc_context.stop();
        });
    grpc_context.run();
    CHECK_EQ(4, request_count);
}

TEST_CASE_FIXTURE(GrpcRepeatedlyRequestTest, "yield_context repeatedly_request client streaming")
{
    bool is_shutdown{false};
    auto request_count{0};
    this->test(
        &test::v1::Test::AsyncService::RequestClientStreaming, service,
        [&](grpc::ServerContext&, grpc::ServerAsyncReader<test::v1::Response, test::v1::Request> reader,
            asio::yield_context yield)
        {
            test::v1::Request request;
            CHECK(agrpc::read(reader, request, yield));
            CHECK_EQ(42, request.integer());
            test::v1::Response response;
            response.set_integer(21);
            ++request_count;
            if (request_count > 3)
            {
                is_shutdown = true;
            }
            CHECK(agrpc::finish(reader, response, grpc::Status::OK, yield));
        },
        [&](asio::yield_context yield)
        {
            while (!is_shutdown)
            {
                test::v1::Response response;
                grpc::ClientContext new_client_context;
                auto [writer, ok] = agrpc::request(&test::v1::Test::Stub::AsyncClientStreaming, *stub,
                                                   new_client_context, response, yield);
                CHECK(ok);
                test::v1::Request request;
                request.set_integer(42);
                CHECK(agrpc::write(*writer, request, yield));
                CHECK(agrpc::writes_done(*writer, yield));
                grpc::Status status;
                CHECK(agrpc::finish(*writer, status, yield));
                CHECK(status.ok());
                CHECK_EQ(21, response.integer());
            }
            grpc_context.stop();
        });
    grpc_context.run();
    CHECK_EQ(4, request_count);
}

TEST_CASE_FIXTURE(test::GrpcClientServerTest, "RPC step after grpc_context stop")
{
    std::optional<bool> ok;
    asio::spawn(get_executor(),
                [&](asio::yield_context yield)
                {
                    grpc_context.stop();
                    test::v1::Request request;
                    grpc::ServerAsyncResponseWriter<test::v1::Response> writer{&server_context};
                    ok = agrpc::request(&test::v1::Test::AsyncService::RequestUnary, service, server_context, request,
                                        writer, yield);
                });
    grpc_context.run();
    CHECK_FALSE(ok);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "asio::post an Alarm and use variadic-arg callback for its wait")
{
    bool ok = false;
    grpc::Alarm alarm;
    asio::post(get_executor(),
               [&]
               {
                   agrpc::wait(alarm, test::ten_milliseconds_from_now(),
                               asio::bind_executor(get_executor(),
                                                   [&](auto&&... args)
                                                   {
                                                       ok = bool{args...};
                                                   }));
               });
    grpc_context.run();
    CHECK(ok);
}

#ifdef AGRPC_ASIO_HAS_CANCELLATION_SLOT
TEST_CASE_FIXTURE(test::GrpcContextTest, "cancel grpc::Alarm with cancellation_type::total")
{
    bool ok = true;
    asio::cancellation_signal signal{};
    grpc::Alarm alarm;
    asio::post(get_executor(),
               [&]
               {
                   agrpc::wait(alarm, std::chrono::system_clock::now() + std::chrono::seconds(3),
                               asio::bind_cancellation_slot(signal.slot(), asio::bind_executor(get_executor(),
                                                                                               [&](bool alarm_ok)
                                                                                               {
                                                                                                   ok = alarm_ok;
                                                                                               })));
                   asio::post(get_executor(),
                              [&]
                              {
                                  signal.emit(asio::cancellation_type::total);
                              });
               });
    grpc_context.run();
    CHECK_FALSE(ok);
}

TEST_CASE_FIXTURE(test::GrpcContextTest, "cancel grpc::Alarm with cancellation_type::none")
{
    bool ok = false;
    asio::cancellation_signal signal{};
    grpc::Alarm alarm;
    asio::post(get_executor(),
               [&]
               {
                   agrpc::wait(alarm, test::hundred_milliseconds_from_now(),
                               asio::bind_cancellation_slot(signal.slot(), asio::bind_executor(get_executor(),
                                                                                               [&](bool alarm_ok)
                                                                                               {
                                                                                                   ok = alarm_ok;
                                                                                               })));
                   asio::post(get_executor(),
                              [&]
                              {
                                  signal.emit(asio::cancellation_type::none);
                              });
               });
    grpc_context.run();
    CHECK(ok);
}
#endif

TEST_SUITE_END();
}  // namespace test_asio_grpc