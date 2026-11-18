# 프로젝트 타깃 공통 컴파일 옵션 (ADR-0004 결정론, ADR-0005 툴체인)
#
# 부동소수점 완화 옵션(/fp:fast, -ffast-math)은 금지예요.
# MSVC 2022의 /fp:precise는 FMA 축약을 하지 않아요(/fp:contract를 따로 켜지 않는 한).

function(sim_apply_compile_options target)
  if(MSVC)
    target_compile_options(${target} PRIVATE
      /W4
      /permissive-
      /utf-8
      /fp:precise
      /Zc:__cplusplus
      /EHsc)
    if(SIM_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -ffp-contract=off
      -fno-fast-math)
    if(SIM_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
