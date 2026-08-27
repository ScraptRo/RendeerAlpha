# The layout toolchain: `rda layout` compiles a .tsx into a blueprint, `rda types`
# emits the definitions layouts are written against. Both need the widget schema and
# QuickJS, which is why this is a program and not a shell script.
#
# The intent is that a build invokes it rather than a person doing so by hand, the way
# shaders are compiled without anyone thinking about it.
add_executable(rda rda.cpp)
target_link_libraries(rda PRIVATE rendeer)
