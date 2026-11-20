# Sidecar's one change to JUCE (Windows): a switch that makes a window take mouse
# input at a point even where another window covers it, or while it's minimised.
# The web remote replays a browser's clicks into Sidecar's windows; without it,
# JUCE asks Windows which window is on top at that point and drops the click
# whenever Sidecar is covered or minimised.
#
# sidecar_patch_juce(<JUCE source dir> <result variable>): applies the change once
# (it is kept on later runs) and sets the variable to TRUE when JUCE has it.
function(sidecar_patch_juce juce_dir result)
    set(${result} FALSE PARENT_SCOPE)
    set(file "${juce_dir}/modules/juce_gui_basics/native/juce_Windowing_windows.cpp")
    if(NOT EXISTS "${file}")
        return()
    endif()
    file(READ "${file}" text)
    set(marker "sidecarIgnoreWindowOverlap")
    string(FIND "${text}" "${marker}" found)
    if(found EQUAL -1)
        set(head "namespace juce\n{\n")
        set(peer "    bool contains (Point<int> localPos, bool trueIfInAChildWindow) const override\n    {\n")
        string(FIND "${text}" "${head}" at_head)
        string(FIND "${text}" "${peer}" at_peer)
        if(at_head EQUAL -1 OR at_peer EQUAL -1)
            message(WARNING "Sidecar: JUCE's Windows peer has changed; the web remote can't click into a covered or minimised window")
            return()
        endif()
        # Only the first namespace opening gets the switch.
        string(SUBSTRING "${text}" 0 ${at_head} before)
        string(LENGTH "${head}" head_len)
        math(EXPR rest_at "${at_head} + ${head_len}")
        string(SUBSTRING "${text}" ${rest_at} -1 rest)
        string(REPLACE "${peer}" "${peer}        if (sidecarIgnoreWindowOverlap) // Sidecar: input replayed by its web remote\n            return true;\n\n" rest "${rest}")
        set(text "${before}${head}\n// Sidecar: while set, windows take mouse input even where covered or minimised.\nbool sidecarIgnoreWindowOverlap = false;\n${rest}")
        file(WRITE "${file}" "${text}")
        message(STATUS "Sidecar: patched JUCE's window overlap check for the web remote")
    endif()
    set(${result} TRUE PARENT_SCOPE)
endfunction()
