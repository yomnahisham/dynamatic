add_executable(export-asic
  export-asic.cpp
)

target_link_libraries(export-asic
  DynamaticSupport
  DynamaticHandshake
  DynamaticHW
  DynamaticConversion
  DynamaticExperimentalSupport
  MLIRIR
  MLIRSupport
  MLIRParser
  LLVMSupport
  LLVMCore
)

# Add to dynamatic-opt if it exists
if(TARGET dynamatic-opt)
  target_link_libraries(dynamatic-opt export-asic)
endif()
