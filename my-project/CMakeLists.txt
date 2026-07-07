set(target_name my-project-rate-control)
set(target_prefix scratch_my-project_)

find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

set(source_files
  rate-control-main.cc
  learning-rate-manager.cc
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
  libai
  ${LIB_AS_NEEDED_POST}
)

pybind11_add_module(my_project_rate_control_py rate-control-ai-py.cc)
set_target_properties(my_project_rate_control_py PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(
  my_project_rate_control_py
  PRIVATE
  libcore
  libai
)
add_dependencies(${target_prefix}${target_name} my_project_rate_control_py)

set_runtime_outputdirectory(
  ${target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/my-project/
  ${target_prefix}
)
