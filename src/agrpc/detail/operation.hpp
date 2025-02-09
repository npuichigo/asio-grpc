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

#ifndef AGRPC_DETAIL_GRPCEXECUTOROPERATION_HPP
#define AGRPC_DETAIL_GRPCEXECUTOROPERATION_HPP

#include "agrpc/detail/memory.hpp"
#include "agrpc/detail/typeErasedOperation.hpp"
#include "agrpc/detail/utility.hpp"

namespace agrpc::detail
{
template <bool IsIntrusivelyListable, class Handler, class Allocator, class Signature, class... ExtraArgs>
class Operation;

template <bool IsIntrusivelyListable, class Handler, class Allocator, class R, class... Signature, class... ExtraArgs>
class Operation<IsIntrusivelyListable, Handler, Allocator, R(Signature...), ExtraArgs...>
    : public detail::TypeErasedOperation<IsIntrusivelyListable, Signature..., ExtraArgs...>
{
  private:
    using Base = detail::TypeErasedOperation<IsIntrusivelyListable, Signature..., ExtraArgs...>;

  public:
    template <class H>
    Operation(H&& handler, Allocator allocator)
        : Base(&Operation::do_complete), impl(std::forward<H>(handler), std::move(allocator))
    {
    }

    static void do_complete(Base* op, detail::InvokeHandler invoke_handler, Signature... args, ExtraArgs...)
    {
        auto* self = static_cast<Operation*>(op);
        detail::RebindAllocatedPointer<Operation, Allocator> ptr{self, self->get_allocator()};
        if (detail::InvokeHandler::YES == invoke_handler)
        {
            auto handler{std::move(self->handler())};
            ptr.reset();
            std::move(handler)(detail::forward_as<Signature>(args)...);
        }
    }

    [[nodiscard]] constexpr decltype(auto) handler() noexcept { return impl.first(); }

    [[nodiscard]] constexpr decltype(auto) handler() const noexcept { return impl.first(); }

    [[nodiscard]] constexpr decltype(auto) get_allocator() noexcept { return impl.second(); }

    [[nodiscard]] constexpr decltype(auto) get_allocator() const noexcept { return impl.second(); }

  private:
    detail::CompressedPair<Handler, Allocator> impl;
};

template <bool IsIntrusivelyListable, class Handler, class Signature>
class LocalOperation;

template <bool IsIntrusivelyListable, class Handler, class R, class... Signature>
class LocalOperation<IsIntrusivelyListable, Handler, R(Signature...)>
    : public detail::TypeErasedOperation<IsIntrusivelyListable, Signature..., detail::GrpcContextLocalAllocator>
{
  private:
    using Base = detail::TypeErasedOperation<IsIntrusivelyListable, Signature..., detail::GrpcContextLocalAllocator>;

  public:
    template <class H>
    explicit LocalOperation(H&& handler) : Base(&LocalOperation::do_complete), handler_(std::forward<H>(handler))
    {
    }

    static void do_complete(Base* op, detail::InvokeHandler invoke_handler, Signature... args,
                            detail::GrpcContextLocalAllocator allocator)
    {
        auto* self = static_cast<LocalOperation*>(op);
        detail::RebindAllocatedPointer<LocalOperation, detail::GrpcContextLocalAllocator> ptr{self, allocator};
        if (detail::InvokeHandler::YES == invoke_handler)
        {
            auto handler{std::move(self->handler_)};
            ptr.reset();
            std::move(handler)(detail::forward_as<Signature>(args)...);
        }
    }

    [[nodiscard]] constexpr auto& handler() noexcept { return handler_; }

    [[nodiscard]] constexpr const auto& handler() const noexcept { return handler_; }

  private:
    Handler handler_;
};
}  // namespace agrpc::detail

#endif  // AGRPC_DETAIL_GRPCEXECUTOROPERATION_HPP
