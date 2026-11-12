# Warnings only on our targets (third parties do not get the flags).
function(oly_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  if(OLY_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()
