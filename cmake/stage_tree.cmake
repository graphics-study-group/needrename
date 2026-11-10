# Copy one source tree into the staging directory, replacing what was there.
#
# A staging category is a copy of a source tree minus the patterns that a
# distribution must not contain, and it must never keep a file the source no
# longer has. The destination is therefore removed before the copy: a deleted
# source file cannot leave a stale copy behind, which is the failure a plain
# merge-copy produces.
#
# Invoked with:
#   -DSOURCE=<dir>        tree whose contents are copied
#   -DDESTINATION=<dir>   staged category directory
#   -DEXCLUDES=<list>     `file(COPY)` exclude patterns, semicolon separated
#   -DSTAMP=<file>        touched last, so a failed run cannot look up to date

foreach(required SOURCE DESTINATION STAMP)
    if (NOT ${required})
        message(FATAL_ERROR "stage_tree: ${required} is not set.")
    endif()
endforeach()

file(REMOVE_RECURSE "${DESTINATION}")
file(MAKE_DIRECTORY "${DESTINATION}")

set(exclude_arguments "")
foreach(pattern IN LISTS EXCLUDES)
    list(APPEND exclude_arguments PATTERN "${pattern}" EXCLUDE)
endforeach()

file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" ${exclude_arguments})

# Stamp last: a failed staging run must not look up to date.
file(TOUCH "${STAMP}")
