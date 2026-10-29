list(APPEND SOURCES "${CMAKE_CURRENT_LIST_DIR}/dsh_animation_player.cc")
# rust/CMakeLists.txt is included after idf_component_register: its target links
# the archive to COMPONENT_LIB, which does not exist during source collection.
