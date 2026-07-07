set(target_name rate-adaptation-main)
set(target_prefix scratch_my-project_)

set(source_files
  rate-adaptation-main.cc
  wifi-mcs-manager.cc
  wifi-environment.cc
  ideal-throughput-manager.cc
  minstrel-agent.cc
  mab-agent.cc
  dqn-agent.cc
)

add_executable(${target_prefix}${target_name} ${source_files})

target_link_libraries(
  ${target_prefix}${target_name}
  ${LIB_AS_NEEDED_PRE}
  ${libcore}
  ${LIB_AS_NEEDED_POST}
)

set_runtime_outputdirectory(
  ${target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/my-project/
  ${target_prefix}
)