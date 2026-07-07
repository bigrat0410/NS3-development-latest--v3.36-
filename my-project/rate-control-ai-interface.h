/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef MY_PROJECT_RATE_CONTROL_AI_INTERFACE_H
#define MY_PROJECT_RATE_CONTROL_AI_INTERFACE_H

#include "ns3/ai-module.h"

#include <cstdint>

namespace ns3
{

struct MyProjectRateControlEnv
{
  uint32_t step{0};
  uint32_t numActions{0};
  uint32_t lastAction{1};
  uint32_t lastMcs{0};
  uint32_t lastSuccess{1};
  uint32_t consecutiveFailures{0};
  double snrDb{15.0};
  double meanSnrDb{15.0};
  double stdSnrDb{0.0};
  double ackRatio{1.0};
  double selectedRateMbps{0.0};
  double bestRateMbps{0.0};
  double reward{0.0};
};

struct MyProjectRateControlAct
{
  double nextAction{1.0};
  double nextMcs{0.0};
  double epsilon{0.0};
};

using MyProjectRateControlInterface =
    Ns3AiMsgInterfaceImpl<MyProjectRateControlEnv, MyProjectRateControlAct>;

} // namespace ns3

#endif // MY_PROJECT_RATE_CONTROL_AI_INTERFACE_H
