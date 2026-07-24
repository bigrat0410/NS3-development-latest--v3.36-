#include "rl-env.h"

#include <pybind11/pybind11.h>

namespace py = pybind11;

//创建Python模块reproduction_reinrate_py，只负责暴露共享数据和同步接口
PYBIND11_MODULE (reproduction_reinrate_py, module)
{
  //Observation：Python读取C++采集到的网络状态
  py::class_<ns3::AiConstantRateEnv> (module, "PyEnvStruct")
      .def (py::init<> ())
      .def_readwrite ("mcs", &ns3::AiConstantRateEnv::mcs)
      .def_readwrite ("max_mcs", &ns3::AiConstantRateEnv::max_mcs)
      .def_readwrite ("cw", &ns3::AiConstantRateEnv::cw)
      .def_readwrite ("throughput", &ns3::AiConstantRateEnv::throughput)
      .def_readwrite ("snr", &ns3::AiConstantRateEnv::snr)
      .def_readwrite ("raw_reward", &ns3::AiConstantRateEnv::raw_reward)
      .def_readwrite ("simulation_time", &ns3::AiConstantRateEnv::simulation_time)
      .def_readwrite ("aggregate_mpdus", &ns3::AiConstantRateEnv::aggregate_mpdus)
      .def_readwrite ("successful_mpdus", &ns3::AiConstantRateEnv::successful_mpdus)
      .def_readwrite ("failed_mpdus", &ns3::AiConstantRateEnv::failed_mpdus);

  //Action：Python写回下一统计窗口要执行的动作
  py::class_<ns3::AiConstantRateAct> (module, "PyActStruct")
      .def (py::init<> ())
      .def_readwrite ("nss", &ns3::AiConstantRateAct::nss)
      .def_readwrite ("next_mcs", &ns3::AiConstantRateAct::next_mcs);

  //把ns3-ai的共享内存和信号量接口暴露给Python
  using MsgInterface =
      ns3::Ns3AiMsgInterfaceImpl<ns3::AiConstantRateEnv, ns3::AiConstantRateAct>;

  py::class_<MsgInterface> (module, "Ns3AiMsgInterfaceImpl")
      .def (py::init<bool,
                     bool,
                     bool,
                     std::uint32_t,
                     const char*,
                     const char*,
                     const char*,
                     const char*> ())
      .def ("PyRecvBegin", &MsgInterface::PyRecvBegin)
      .def ("PyRecvEnd", &MsgInterface::PyRecvEnd)
      .def ("PySendBegin", &MsgInterface::PySendBegin)
      .def ("PySendEnd", &MsgInterface::PySendEnd)
      .def ("PyGetFinished", &MsgInterface::PyGetFinished)
      .def ("PyReset", &MsgInterface::PyReset)
      .def ("GetCpp2PyStruct",
            &MsgInterface::GetCpp2PyStruct,
            py::return_value_policy::reference)
      .def ("GetPy2CppStruct",
            &MsgInterface::GetPy2CppStruct,
            py::return_value_policy::reference);
}
