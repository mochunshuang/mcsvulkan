function(copy_dir_to_bindir DIR)
    if(EXISTS "${EXE_DIR}/${DIR}")
        file(COPY "${EXE_DIR}/${DIR}"
            DESTINATION ${OUTPUT_DIRECTORY})
    endif()
endfunction()
