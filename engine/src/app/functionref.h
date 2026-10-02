// SPDX-License-Identifier: GPL-3.0-or-later
// A NON-OWNING REFERENCE TO A CALLABLE - two pointers and one indirect call,
// no allocation (unlike `std::function`). `todo/play-split.md`: `main`'s
// lambdas live as long as `main`, and the frame (`backends/sdl/playframe.h`)
// reaches them through one of these, under the lambda's own name, so the
// code that calls them did not change. It must never outlive what it refers
// to; built from a temporary, it is good for that full expression only.
#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace omk {

template <class Sig> class FunctionRef;

template <class R, class... A>
class FunctionRef<R(A...)> {
public:
    template <class F, class = std::enable_if_t<
                           !std::is_same_v<std::remove_cvref_t<F>, FunctionRef> &&
                           std::is_invocable_r_v<R, F&, A...>>>
    FunctionRef(F&& f) noexcept
        : obj_(const_cast<void*>(static_cast<const void*>(std::addressof(f)))),
          call_([](void* o, A... a) -> R {
              return (*static_cast<std::remove_reference_t<F>*>(o))(std::forward<A>(a)...);
          }) {}

    R operator()(A... a) const { return call_(obj_, std::forward<A>(a)...); }

private:
    void* obj_;
    R (*call_)(void*, A...);
};

}  // namespace omk
