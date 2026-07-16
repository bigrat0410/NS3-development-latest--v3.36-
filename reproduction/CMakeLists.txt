set(target_name two-node-ht)
set(target_prefix scratch_reproduction_)

add_executable(${target_prefix}${target_name} two-node-ht.cc rl-env.cc)

target_link_libraries(
  ${target_prefix}${target_name}
  ${LIB_AS_NEEDED_PRE}
  libcore
  libmobility
  libnetwork
  libinternet
  libapplications
  libpropagation
  libwifi
  libai
  ${LIB_AS_NEEDED_POST}
)

set_runtime_outputdirectory(
  ${target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/reproduction/
  ${target_prefix}
)

#构建C++/Python共享内存绑定模块；它只负责通信，不包含强化学习策略
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(reproduction_reinrate_py reinrate_py.cc)
set_target_properties(
  reproduction_reinrate_py
  PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(
  reproduction_reinrate_py
  PRIVATE
  libcore
  libnetwork
  libwifi
  libai
)
