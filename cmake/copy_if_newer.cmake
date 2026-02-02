# copy_if_newer.cmake
# Copies SOURCE to DEST only if:
#   - DEST doesn't exist, OR
#   - SOURCE is newer than DEST
#
# Usage:
#   cmake -DSOURCE=path/to/source -DDEST=path/to/dest -P copy_if_newer.cmake

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "SOURCE not defined")
endif()

if(NOT DEFINED DEST)
    message(FATAL_ERROR "DEST not defined")
endif()

if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "Source file does not exist: ${SOURCE}")
endif()

# If destination doesn't exist, copy
if(NOT EXISTS "${DEST}")
    message(STATUS "Copying ${SOURCE} -> ${DEST} (destination doesn't exist)")
    file(COPY "${SOURCE}" DESTINATION "${DEST}/..")
else()
    # Compare timestamps
    file(TIMESTAMP "${SOURCE}" SOURCE_TIME "%Y%m%d%H%M%S")
    file(TIMESTAMP "${DEST}" DEST_TIME "%Y%m%d%H%M%S")

    if("${SOURCE_TIME}" STRGREATER "${DEST_TIME}")
        message(STATUS "Copying ${SOURCE} -> ${DEST} (source is newer)")
        file(COPY "${SOURCE}" DESTINATION "${DEST}/..")
    else()
        message(STATUS "Skipping ${SOURCE} (destination is newer or same age)")
    endif()
endif()
