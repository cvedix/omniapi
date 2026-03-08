# Optional: shared library for LD_PRELOAD to send TCP FIN on socket close.
# Use when the SDK (cvedix) does not call shutdown(SHUT_RDWR) before close on rtmp_des.
add_library(close_fin SHARED close_fin.c)
target_compile_options(close_fin PRIVATE -fPIC -Wall)
target_link_libraries(close_fin PRIVATE dl)
set_target_properties(close_fin PROPERTIES
  OUTPUT_NAME "close_fin"
  PREFIX "lib"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)
install(TARGETS close_fin LIBRARY DESTINATION lib)
