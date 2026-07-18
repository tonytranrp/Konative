# The only .cpp translation units in the whole project (ARCHITECTURE.md section 2): genuinely
# load-bearing entry points, not a default home for "regular" code - that all lives in
# include/konative/**.hpp.
if(ANDROID)
  add_subdirectory(platform/android)
endif()
