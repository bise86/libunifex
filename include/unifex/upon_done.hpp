/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <unifex/config.hpp>
#include <unifex/bind_back.hpp>
#include <unifex/continuations.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/sender_concepts.hpp>
#include <unifex/std_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/type_list.hpp>
#include <unifex/type_traits.hpp>

#include <functional>

#include <unifex/detail/prologue.hpp>

namespace unifex {
namespace _upon_done {
namespace detail {
template <typename Result, typename = void>
struct result_overload {
  using type = type_list<Result>;
};
template <>
struct result_overload<void> {
  using type = type_list<>;
};

template <typename Result>
using result_overload_t = typename result_overload<Result>::type;

}  // namespace detail

template <typename Receiver, typename Func>
struct _receiver {
  struct type;
};
template <typename Receiver, typename Func>
using receiver_t = typename _receiver<Receiver, Func>::type;

template <typename Receiver, typename Func>
struct _receiver<Receiver, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;
  UNIFEX_NO_UNIQUE_ADDRESS Receiver receiver_;

  template(typename... Values)  //
      (requires receiver_of<
          Receiver,
          Values...>)  //
      void set_value(Values&&... values) && {
    unifex::set_value((Receiver&&)(receiver_), (Values&&)(values)...);
  }

  template(typename Error)                  //
      (requires receiver<Receiver, Error>)  //
      void set_error(Error&& error) && noexcept {
    unifex::set_error((Receiver&&)(receiver_), (Error&&)(error));
  }

  void set_done() && noexcept {
    using result_t = std::invoke_result_t<Func>;
    if constexpr (std::is_void_v<result_t>) {
      if constexpr (noexcept(std::invoke((Func&&)func_))) {
        std::invoke((Func&&)func_);
        unifex::set_value((Receiver&&)receiver_);
      } else {
        UNIFEX_TRY {
          std::invoke((Func&&)func_);
          unifex::set_value((Receiver&&)receiver_);
        }
        UNIFEX_CATCH(...) {
          unifex::set_error((Receiver&&)receiver_, std::current_exception());
        }
      }
    } else {
      if constexpr (noexcept(std::invoke((Func&&)func_))) {
        unifex::set_value((Receiver&&)receiver_, std::invoke((Func&&)func_));
      } else {
        UNIFEX_TRY {
          unifex::set_value((Receiver&&)receiver_, std::invoke((Func&&)func_));
        }
        UNIFEX_CATCH(...) {
          unifex::set_error((Receiver&&)receiver_, std::current_exception());
        }
      }
    }
  }

  template(typename CPO)                       //
      (requires is_receiver_query_cpo_v<CPO>)  //
      friend auto tag_invoke(CPO cpo, const type& r) noexcept(
          std::is_nothrow_invocable_v<CPO, const Receiver&>)
          -> std::invoke_result_t<CPO, const Receiver&> {
    return std::move(cpo)(std::as_const(r.receiver_));
  }

#if UNIFEX_ENABLE_CONTINUATION_VISITATIONS
  template <typename Visit>
  friend void
  tag_invoke(tag_t<visit_continuations>, const type& self, Visit&& visit) {
    std::invoke(visit, self.receiver_);
  }
#endif
};

template <typename Predecessor, typename Func>
struct _sender {
  struct type;
};
template <typename Predecessor, typename Func>
using sender =
    typename _sender<remove_cvref_t<Predecessor>, std::decay_t<Func>>::type;

template <typename Predecessor, typename Func>
struct _sender<Predecessor, Func>::type {
  UNIFEX_NO_UNIQUE_ADDRESS Predecessor pred_;
  UNIFEX_NO_UNIQUE_ADDRESS Func func_;

private:
  /*
   * This helper returns type_list<type_list<Result>> if func returns Result
   * else if func returns void then helper returns type_list<type_list<>>
   */
  using invoked_result_t =
      type_list<detail::result_overload_t<std::invoke_result_t<Func>>>;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using sets_done_predecessor_value_type_t = type_list_nested_apply_t<
      concat_type_lists_unique_t<
          sender_value_types_t<Predecessor, type_list, type_list>,
          invoked_result_t>,
      Variant,
      Tuple>;

  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using no_sets_done_predecessor_value_type_t = type_list_nested_apply_t<
      sender_value_types_t<Predecessor, type_list, type_list>,
      Variant,
      Tuple>;

public:
  template <
      template <typename...>
      class Variant,
      template <typename...>
      class Tuple>
  using value_types = std::conditional_t<
      Predecessor::sends_done,
      sets_done_predecessor_value_type_t<Variant, Tuple>,
      no_sets_done_predecessor_value_type_t<Variant, Tuple>>;

  template <template <typename...> class Variant>
  using error_types = typename concat_type_lists_unique_t<
      sender_error_types_t<Predecessor, type_list>,
      type_list<std::exception_ptr>>::template apply<Variant>;

  static constexpr bool sends_done = false;

  static constexpr blocking_kind blocking =
      sender_traits<Predecessor>::blocking;

  static constexpr bool is_always_scheduler_affine =
      sender_traits<Predecessor>::is_always_scheduler_affine;

  template <typename Receiver>
  using receiver_t = receiver_t<Receiver, Func>;

  friend constexpr blocking_kind
  tag_invoke(tag_t<blocking>, const type& sender) {
    return unifex::blocking(sender.pred_);
  }

  template(typename Sender, typename Receiver)  //
      (requires same_as<remove_cvref_t<Sender>, type> AND receiver<Receiver> AND
           sender_to<
               member_t<Sender, Predecessor>,
               receiver_t<remove_cvref_t<Receiver>>>)  //
      friend auto tag_invoke(tag_t<unifex::connect>, Sender&& s, Receiver&& r) noexcept(
	  std::is_nothrow_constructible_v<remove_cvref_t<Receiver>, Receiver> &&
          std::is_nothrow_constructible_v<Func, member_t<Sender, Func>> &&
          is_nothrow_connectable_v<
              member_t<Sender, Predecessor>,
              receiver_t<remove_cvref_t<Receiver>>>)
          -> connect_result_t<
              member_t<Sender, Predecessor>,
              receiver_t<remove_cvref_t<Receiver>>> {
    return unifex::connect(
        static_cast<Sender&&>(s).pred_,
        receiver_t<remove_cvref_t<Receiver>>{
            static_cast<Sender&&>(s).func_, static_cast<Receiver&&>(r)});
  }
};

namespace _cpo {
struct _fn {
private:
  template <typename Sender, typename Func>
  using _result_t = typename conditional_t<
      tag_invocable<_fn, Sender, Func>,
      meta_tag_invoke_result<_fn>,
      meta_quote2<_upon_done::sender>>::template apply<Sender, Func>;

public:
  template(typename Sender, typename Func)  //
      (requires std::is_invocable_v<Func> AND
           tag_invocable<_fn, Sender, Func>)  //
      auto
      operator()(Sender&& predecessor, Func&& func) const
      noexcept(is_nothrow_tag_invocable_v<_fn, Sender, Func>)
          -> _result_t<Sender, Func> {
    return unifex::tag_invoke(_fn{}, (Sender&&)(predecessor), (Func&&)(func));
  }

  template(typename Sender, typename Func)  //
      (requires(!tag_invocable<_fn, Sender, Func>)
           AND std::is_invocable_v<Func>)  //
      auto
      operator()(Sender&& predecessor, Func&& func) const
      noexcept(std::is_nothrow_constructible_v<
               _upon_done::sender<Sender, Func>,
               Sender,
               Func>) -> _result_t<Sender, Func> {
    return _upon_done::sender<Sender, Func>{
        (Sender&&)(predecessor), (Func&&)(func)};
  }
  template(typename Func)                   //
      (requires std::is_invocable_v<Func>)  //
      constexpr auto
      operator()(Func&& func) const
      noexcept(std::is_nothrow_invocable_v<tag_t<bind_back>, _fn, Func>)
          -> bind_back_result_t<_fn, Func> {
    return bind_back(*this, (Func&&)(func));
  }
};
}  // namespace _cpo
}  // namespace _upon_done
inline constexpr _upon_done::_cpo::_fn upon_done{};
}  // namespace unifex

#include <unifex/detail/epilogue.hpp>
