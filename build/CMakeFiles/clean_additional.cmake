# Additional clean files
cmake_minimum_required(VERSION 3.16)

if("${CONFIG}" STREQUAL "" OR "${CONFIG}" STREQUAL "Debug")
  file(REMOVE_RECURSE
  "CMakeFiles/GuitarPi_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/GuitarPi_autogen.dir/ParseCache.txt"
  "CMakeFiles/TabPagePreview_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/TabPagePreview_autogen.dir/ParseCache.txt"
  "CMakeFiles/guitarpi_tab_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/guitarpi_tab_autogen.dir/ParseCache.txt"
  "CMakeFiles/tab_module_autogen.dir/AutogenUsed.txt"
  "CMakeFiles/tab_module_autogen.dir/ParseCache.txt"
  "GuitarPi_autogen"
  "TabPagePreview_autogen"
  "guitarpi_tab_autogen"
  "tab_module_autogen"
  )
endif()
