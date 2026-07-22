set(target_name round2-scenario1)
set(target_prefix scratch_reproduction_round2_)

add_executable(${target_prefix}${target_name} rate-control.cc)

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
  libflow-monitor
  ${LIB_AS_NEEDED_POST}
)

set_runtime_outputdirectory(
  ${target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/reproduction_round2/
  ${target_prefix}
)
