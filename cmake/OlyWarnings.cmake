# Avisos só nos nossos alvos (terceiros não recebem as flags).
function(oly_warnings target)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
  if(OLY_WERROR)
    target_compile_options(${target} PRIVATE -Werror)
  endif()
endfunction()
