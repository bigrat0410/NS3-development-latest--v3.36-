set(target_name two-node-ht)
set(target_prefix scratch_reproduction_)

add_executable(${target_prefix}${target_name} two-node-ht.cc)

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
  ${LIB_AS_NEEDED_POST}
)

set_runtime_outputdirectory(
  ${target_name}
  ${CMAKE_OUTPUT_DIRECTORY}/scratch/reproduction/
  ${target_prefix}
)
