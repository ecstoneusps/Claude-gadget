# Makes esp-dl compile all of its ops (see gen_all_ops_req.py for why).
idf_component_register()

idf_build_get_property(build_components BUILD_COMPONENTS)
if("espressif__esp-dl" IN_LIST build_components)
    idf_component_get_property(espdl_dir espressif__esp-dl COMPONENT_DIR)
elseif("esp-dl" IN_LIST build_components)
    idf_component_get_property(espdl_dir esp-dl COMPONENT_DIR)
endif()

if(espdl_dir)
    idf_build_get_property(python PYTHON)
    set(req_yml "${CMAKE_CURRENT_BINARY_DIR}/all_ops.compile_req.yml")
    execute_process(
        COMMAND ${python} "${CMAKE_CURRENT_LIST_DIR}/gen_all_ops_req.py"
                --ops "${espdl_dir}/spec/ops.yml"
                --kernels "${espdl_dir}/spec/kernels.yml"
                --out "${req_yml}"
        RESULT_VARIABLE rv)
    if(NOT rv EQUAL 0)
        message(FATAL_ERROR "espdl_all_ops: generating ${req_yml} failed")
    endif()
    include(${espdl_dir}/cmake/compile_register.cmake)
    esp_dl_compile_add_yml(${req_yml})
endif()
