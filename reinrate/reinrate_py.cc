#include "rl-env.h"

#include <ns3/ai-module.h>

#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(reinrate_py, m)
{
  py::class_<ns3::AiConstantRateEnv>(m, "PyEnvStruct")
      .def(py::init<>())
      .def_readwrite("mcs", &ns3::AiConstantRateEnv::mcs)
      .def_readwrite("max_mcs", &ns3::AiConstantRateEnv::max_mcs)
      .def_readwrite("cw", &ns3::AiConstantRateEnv::cw)
      .def_readwrite("throughput", &ns3::AiConstantRateEnv::throughput)
      .def_readwrite("snr", &ns3::AiConstantRateEnv::snr);

  py::class_<ns3::AiConstantRateAct>(m, "PyActStruct")
      .def(py::init<>())
      .def_readwrite("nss", &ns3::AiConstantRateAct::nss)
      .def_readwrite("next_mcs", &ns3::AiConstantRateAct::next_mcs);

  using MsgInterface = ns3::Ns3AiMsgInterfaceImpl<ns3::AiConstantRateEnv, ns3::AiConstantRateAct>;
  py::class_<MsgInterface>(m, "Ns3AiMsgInterfaceImpl")
      .def(py::init<bool, bool, bool, uint32_t, const char*, const char*, const char*, const char*>())
      .def("PyRecvBegin", &MsgInterface::PyRecvBegin)
      .def("PyRecvEnd", &MsgInterface::PyRecvEnd)
      .def("PySendBegin", &MsgInterface::PySendBegin)
      .def("PySendEnd", &MsgInterface::PySendEnd)
      .def("PyGetFinished", &MsgInterface::PyGetFinished)
      .def("GetCpp2PyStruct", &MsgInterface::GetCpp2PyStruct, py::return_value_policy::reference)
      .def("GetPy2CppStruct", &MsgInterface::GetPy2CppStruct, py::return_value_policy::reference);
}
