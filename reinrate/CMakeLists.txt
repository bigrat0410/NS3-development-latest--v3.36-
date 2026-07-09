set(target_name rate-control)
set(target_prefix scratch_reinrate_)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

set(source_files
  rate-control.cc
  rl-env.cc
)

add_executable(${target_prefix}${target_name} ${source_files})

target_link_libraries(
  ${target_prefix}${target_name}
  ${LIB_AS_NEEDED_PRE}
  libcore
  libnetwork
  libinternet
  libapplications
  libmobility
  libpropagation
  libwifi
  libflow-monitor
  libai
  ${LIB_AS_NEEDED_POST}
)

set_runtime_outputdirectory(
  ${target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/reinrate/
  ${target_prefix}
)

pybind11_add_module(reinrate_py reinrate_py.cc)
set_target_properties(reinrate_py PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(
  reinrate_py
  PRIVATE
  libcore
  libnetwork
  libwifi
  libai
)
add_dependencies(${target_prefix}${target_name} reinrate_py)

set(scenario1_target_name reinrate-scenario1)

add_executable(${target_prefix}${scenario1_target_name}
  scenario1.cc
  rl-env.cc
)

target_link_libraries(
  ${target_prefix}${scenario1_target_name}
  ${LIB_AS_NEEDED_PRE}
  libcore
  libnetwork
  libinternet
  libapplications
  libmobility
  libpropagation
  libwifi
  libai
  ${LIB_AS_NEEDED_POST}
)

set_runtime_outputdirectory(
  ${scenario1_target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/reinrate/
  ${target_prefix}
)

add_dependencies(${target_prefix}${scenario1_target_name} reinrate_py)
