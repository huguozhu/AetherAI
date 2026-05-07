add_library(Aether.Core STATIC)
set_target_properties(Aether.Core PROPERTIES FOLDER "AetherEngine")
target_sources(Aether.Core
    PUBLIC
        FILE_SET CXX_MODULES
        FILES
            src/aether.core.cppm
    PRIVATE
        src/math_impl.cpp
        src/thread_pool.cpp
        src/log_impl.cpp
)
target_include_directories(Aether.Core PUBLIC src)
