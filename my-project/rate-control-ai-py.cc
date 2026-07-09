/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "rate-control-ai-interface.h"

#include <pybind11/pybind11.h>

namespace py = pybind11;

// 这个模块只做一件事：把 C++ 共享内存结构体和 ns3-ai 接口暴露给 Python。
// Python 端 import my_project_rate_control_py 后，就能直接读写 C++ 同一块内存。
PYBIND11_MODULE(my_project_rate_control_py, m)
{
  // C++ -> Python 的环境结构体绑定。def_readwrite 表示 Python 可以直接访问字段，
  // 例如 env.snrDb、env.reward。
  py::class_<ns3::MyProjectRateControlEnv> (m, "PyEnvStruct")
      .def (py::init<> ())
      .def_readwrite ("step", &ns3::MyProjectRateControlEnv::step)
      .def_readwrite ("numActions", &ns3::MyProjectRateControlEnv::numActions)
      .def_readwrite ("lastAction", &ns3::MyProjectRateControlEnv::lastAction)
      .def_readwrite ("lastMcs", &ns3::MyProjectRateControlEnv::lastMcs)
      .def_readwrite ("lastSuccess", &ns3::MyProjectRateControlEnv::lastSuccess)
      .def_readwrite ("consecutiveFailures", &ns3::MyProjectRateControlEnv::consecutiveFailures)
      .def_readwrite ("snrDb", &ns3::MyProjectRateControlEnv::snrDb)
      .def_readwrite ("meanSnrDb", &ns3::MyProjectRateControlEnv::meanSnrDb)
      .def_readwrite ("stdSnrDb", &ns3::MyProjectRateControlEnv::stdSnrDb)
      .def_readwrite ("ackRatio", &ns3::MyProjectRateControlEnv::ackRatio)
      .def_readwrite ("selectedRateMbps", &ns3::MyProjectRateControlEnv::selectedRateMbps)
      .def_readwrite ("bestRateMbps", &ns3::MyProjectRateControlEnv::bestRateMbps)
      .def_readwrite ("reward", &ns3::MyProjectRateControlEnv::reward);

  // Python -> C++ 的动作结构体绑定。Python 写完 act.nextAction 后释放锁，
  // C++ 的 RequestPythonAction 才会继续执行。
  py::class_<ns3::MyProjectRateControlAct> (m, "PyActStruct")
      .def (py::init<> ())
      .def_readwrite ("nextAction", &ns3::MyProjectRateControlAct::nextAction)
      .def_readwrite ("nextMcs", &ns3::MyProjectRateControlAct::nextMcs)
      .def_readwrite ("epsilon", &ns3::MyProjectRateControlAct::epsilon);

  // 绑定 ns3-ai 生成的消息接口。Begin/End 方法用于同步两端共享内存读写。
  py::class_<ns3::MyProjectRateControlInterface> (m, "Ns3AiMsgInterfaceImpl")
      .def (py::init<bool,
                     bool,
                     bool,
                     uint32_t,
                     const char*,
                     const char*,
                     const char*,
                     const char*> ())
      .def ("PyRecvBegin", &ns3::MyProjectRateControlInterface::PyRecvBegin)
      .def ("PyRecvEnd", &ns3::MyProjectRateControlInterface::PyRecvEnd)
      .def ("PySendBegin", &ns3::MyProjectRateControlInterface::PySendBegin)
      .def ("PySendEnd", &ns3::MyProjectRateControlInterface::PySendEnd)
      .def ("PyGetFinished", &ns3::MyProjectRateControlInterface::PyGetFinished)
      .def ("GetCpp2PyStruct",
            &ns3::MyProjectRateControlInterface::GetCpp2PyStruct,
            py::return_value_policy::reference)
      .def ("GetPy2CppStruct",
            &ns3::MyProjectRateControlInterface::GetPy2CppStruct,
            py::return_value_policy::reference);
}
