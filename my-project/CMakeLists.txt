set(target_name my-project-rate-control)
set(target_prefix scratch_my-project_)

# Python 解释器/开发头文件用于编译 pybind11 模块，让 Python 能访问 ns3-ai 共享内存结构。
find_package(Python REQUIRED COMPONENTS Interpreter Development.Module)
find_package(pybind11 CONFIG REQUIRED)

# C++ 仿真主程序：搭建 Wi-Fi 场景，并把 AP 侧速率管理器替换为自定义学习器。
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

# Python 扩展模块：输出到 scratch/my-project 目录，这样 ns3ai_rate_control.py 可直接 import。
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
# 先编译 Python 绑定，再编译/运行仿真程序，避免 Python 驱动启动时找不到 .so。
add_dependencies(${target_prefix}${target_name} my_project_rate_control_py)

# 把 scratch 程序放到 ns-3 约定的 build/scratch/my-project/ 输出目录。
set_runtime_outputdirectory(
  ${target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/my-project/
  ${target_prefix}
)
