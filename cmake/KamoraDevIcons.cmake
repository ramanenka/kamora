function(kamora_mark_dev_icons out_var)
    set(generated)
    foreach(source IN LISTS ARGN)
        get_filename_component(name "${source}" NAME)
        string(REPLACE "io.github.ramanenka.kamora" "io.github.ramanenka.kamoradev" name "${name}")
        set(destination "${CMAKE_CURRENT_BINARY_DIR}/icons/${name}")

        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${source}" svg)
        if(name MATCHES "-tray")
            if(name MATCHES "^22-")
                set(wedge "M0 0H4L0 4Z")
            else()
                set(wedge "M0 0H11.6L0 11.6Z")
            endif()
            string(REPLACE "</svg>"
                           "    <path d=\"${wedge}\" fill=\"currentColor\" class=\"ColorScheme-Text\"/>\n</svg>"
                           svg "${svg}")
        else()
            string(REPLACE "#7f8c8d" "#4a3184" svg "${svg}")
            string(REPLACE "#afb0b3" "#9579d6" svg "${svg}")
        endif()
        file(GENERATE OUTPUT "${destination}" CONTENT "${svg}")

        list(APPEND generated "${destination}")
    endforeach()

    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${ARGN})
    set(${out_var} "${generated}" PARENT_SCOPE)
endfunction()
