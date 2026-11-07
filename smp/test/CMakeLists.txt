######################################################################################
### 0) Include headers
######################################################################################

include_directories(${EO_SRC_DIR}/src)
include_directories(${SMP_SRC_DIR}/src)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

######################################################################################
### 1) Define test list
######################################################################################

set (TEST_LIST  
        t-smpScheduler
        t-smpMW_eoEasyEA
        t-smpMW_eoEasyPSO
        t-smpMW_eoSyncEasyPSO
        t-smpIsland
        t-smpTopo
        t-smpMI_Homogeneous
        t-smpMI_Heterogeneous
        t-smpMI_Wrapper
        t-smpCustomTopo
        t-smpSharedFitContinue
        t-smpModelLifecycle
		)

######################################################################################
### 3) Create each test
######################################################################################

foreach (test ${TEST_LIST})
	set ("T_${test}_SOURCES" "${test}.cpp")
    add_executable(${test} ${T_${test}_SOURCES})
    target_link_libraries(${test} smp eo eoutils)
    add_test(${test} ${test}) 
    install(TARGETS ${test} RUNTIME DESTINATION share${INSTALL_SUB_DIR}/smp/test COMPONENT tests)
endforeach (test)

target_include_directories(t-smpModelLifecycle PRIVATE ${MOEO_SRC_DIR}/src)
add_custom_target(smp-tests DEPENDS ${TEST_LIST})

if(MPI)
    add_executable(t-smpMPIIslandModel t-smpMPIIslandModel.cpp)
    target_link_libraries(t-smpMPIIslandModel smp eo eoutils)
    add_dependencies(smp-tests t-smpMPIIslandModel eompi)
    add_test(NAME t-smpMPIIslandModel
        COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 3 ${MPIEXEC_PREFLAGS}
        $<TARGET_FILE:t-smpMPIIslandModel> ${MPIEXEC_POSTFLAGS})
    set_tests_properties(t-smpMPIIslandModel PROPERTIES TIMEOUT 60 PROCESSORS 3)
endif()
set_tests_properties(t-smpModelLifecycle PROPERTIES TIMEOUT 30)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/t-data.dat
    ${CMAKE_CURRENT_BINARY_DIR}/t-data.dat)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/t-data.dat DESTINATION share${INSTALL_SUB_DIR}/smp/test COMPONENT tests)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/data-topo-bool
    ${CMAKE_CURRENT_BINARY_DIR}/data-topo-bool)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data-topo-bool DESTINATION share${INSTALL_SUB_DIR}/smp/test COMPONENT tests)

execute_process(
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/data-topo-stoch
    ${CMAKE_CURRENT_BINARY_DIR}/data-topo-stoch)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/data-topo-stoch DESTINATION share${INSTALL_SUB_DIR}/smp/test COMPONENT tests)
