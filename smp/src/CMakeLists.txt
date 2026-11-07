######################################################################################
### 0) Include directories
######################################################################################

include_directories(${EO_SRC_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

######################################################################################
### 1) Set paths
######################################################################################

set(SMP_LIB_OUTPUT_PATH ${SMP_BIN_DIR}/lib)
set(LIBRARY_OUTPUT_PATH ${SMP_LIB_OUTPUT_PATH})

######################################################################################
### 2) Build lib
######################################################################################

set (SMP_FILE 
    MWModel.h
    scheduler.h
    island.h
    topology/abstractTopology.h
    topology/topology.h
    topology/topologyBuilder.h
    topology/complete.cpp
    topology/star.cpp
    topology/ring.cpp
    topology/hypercubic.cpp
    topology/mesh.cpp
    topology/customBooleanTopology.cpp
    topology/customStochasticTopology.cpp
    notifier.cpp
    islandModelWrapper.h
    sharedFitContinue.h
    )

add_library(smp STATIC ${SMP_FILE})

target_include_directories(smp PUBLIC
    $<BUILD_INTERFACE:${EO_SRC_DIR}/src>
    $<BUILD_INTERFACE:${SMP_SRC_DIR}/src>
    $<INSTALL_INTERFACE:include${INSTALL_SUB_DIR}/eo>
    $<INSTALL_INTERFACE:include${INSTALL_SUB_DIR}/smp>)
find_package(Threads REQUIRED)
target_link_libraries(smp PUBLIC Threads::Threads eoserial)
if(MPI)
    target_compile_definitions(smp PUBLIC WITH_MPI)
    target_link_libraries(smp PUBLIC MPI::MPI_CXX)
endif()

install(TARGETS smp EXPORT paradiseo-targets ARCHIVE DESTINATION ${LIB} COMPONENT libraries)

######################################################################################
### 3) Look for headers
######################################################################################

file(GLOB HDRS smp *.h *.cpp)
install(FILES ${HDRS} DESTINATION include${INSTALL_SUB_DIR}/smp COMPONENT headers)

######################################################################################
### 4) Install directories
######################################################################################

install(DIRECTORY MWAlgo topology
        DESTINATION include${INSTALL_SUB_DIR}/smp
        COMPONENT headers
        FILES_MATCHING PATTERN "*.h"
        PATTERN "*.cpp"
        )
