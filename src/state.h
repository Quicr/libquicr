#pragma once

#include <quicr/quicr_client.h>
#include <variant>

namespace quicr {

// WIP

struct QuicRClient::State
{

  State() {}

  ~State() = default;

private:
  struct Start
  {};

  // connect
  struct ConnectPending : Start
  {};

  struct Connected
  {};

  struct SubscribePending
  {};

  struct PublishIntentPending
  {};

  struct Finish
  {};

  using Substate = std::variant<Start,
                                ConnectPending,
                                Connected,
                                SubscribePending,
                                PublishIntentPending,
                                Finish>;

  Substate substate;

  template<typename T>
  bool in_substate()
  {
    return std::holds_alternative<T>(substate);
  }

  template<typename From, typename To, typename... Args>
  void transition(Args... args)
  {
    substate =
      To{ std::move(std::get<From>(substate)), std::forward<Args>(args)... };
  }
};

}