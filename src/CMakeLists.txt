cmake_minimum_required (VERSION 3.8)

add_executable (jpeg-dissect 
	"main.c"
	"loader.c"
 )

set_property(TARGET jpeg-dissect PROPERTY C_STANDARD 11)

if (UNIX)
	target_link_libraries(jpeg-dissect m)
endif()