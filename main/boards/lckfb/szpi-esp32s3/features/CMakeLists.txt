# Explicit feature list: adding a module requires naming its build entry here.
# Avoid recursively collecting files from unrelated boards or tool output.
include("${CMAKE_CURRENT_LIST_DIR}/animation/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/pet_display/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/motion/CMakeLists.txt")
include("${CMAKE_CURRENT_LIST_DIR}/expansion_uart/CMakeLists.txt")
