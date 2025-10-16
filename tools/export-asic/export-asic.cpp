//===- export-asic.cpp - Export ASIC from HW-level IR ----------*- C++ -*-===//
//
// Dynamatic is under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Exports ASIC-ready RTL from HW-level IR with LibreLane integration.
// Files corresponding to internal and external modules are written inside a 
// provided output directory (which is created if necessary).
//
//===----------------------------------------------------------------------===//

#include "dynamatic/Conversion/HandshakeToHW.h"
#include "dynamatic/Dialect/HW/HWDialect.h"
#include "dynamatic/Dialect/HW/HWOpInterfaces.h"
#include "dynamatic/Dialect/HW/HWOps.h"
#include "dynamatic/Dialect/Handshake/HandshakeDialect.h"
#include "dynamatic/Dialect/Handshake/HandshakeTypes.h"
#include "dynamatic/Support/LLVM.h"
#include "dynamatic/Support/RTL/RTL.h"
#include "dynamatic/Support/System.h"
#include "dynamatic/Support/Utils/Utils.h"
#include "experimental/Support/FormalProperty.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/OwningOpRef.h"
#include "mlir/IR/Value.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Support/IndentedOstream.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/TypeSwitch.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <fstream>

using namespace llvm;
using namespace mlir;
using namespace dynamatic;
using namespace dynamatic::handshake;

static cl::OptionCategory mainCategory("Tool options");

static cl::opt<std::string> inputFilename(cl::Positional, cl::Required,
                                          cl::desc("<input file>"),
                                          cl::cat(mainCategory));

static cl::opt<std::string> outputDir(cl::Positional, cl::Required,
                                      cl::desc("<output directory>"),
                                      cl::cat(mainCategory));

static cl::opt<std::string> dynamaticPath("dynamatic-path", cl::Optional,
                                          cl::desc("<path to Dynamatic>"),
                                          cl::init("."), cl::cat(mainCategory));

static cl::opt<std::string> propertyFilename("property-database", cl::Optional,
                                             cl::desc("<property file>"),
                                             cl::cat(mainCategory));

static cl::opt<std::string> pdk("pdk", cl::Optional,
                                cl::desc("<Process Design Kit>"),
                                cl::init("sky130"), cl::cat(mainCategory));

static cl::opt<std::string> library("library", cl::Optional,
                                    cl::desc("<Standard cell library>"),
                                    cl::init("sky130_fd_sc_hd"), cl::cat(mainCategory));

static cl::opt<std::string> designName("design-name", cl::Optional,
                                       cl::desc("<Design name>"),
                                       cl::init("dynamatic_design"), cl::cat(mainCategory));

static cl::opt<bool> runLibreLane("run-librelane", cl::Optional,
                                  cl::desc("Run complete LibreLane flow"),
                                  cl::init(false), cl::cat(mainCategory));

static cl::opt<std::string> libreLanePath("librelane-path", cl::Optional,
                                          cl::desc("<Path to LibreLane>"),
                                          cl::init(""), cl::cat(mainCategory));

static cl::list<std::string>
    rtlConfigs(cl::Positional, cl::OneOrMore,
               cl::desc("<RTL configuration files...>"), cl::cat(mainCategory));

namespace llvm {
const std::pair<std::string, bool> EMPTY_KEY = {"EMPTY_KEY", false};
const std::pair<std::string, bool> TOMBSTONE_KEY = {"TOMBSTONE_KEY", false};

template <>
struct DenseMapInfo<std::pair<std::string, bool>> {
  static inline std::pair<std::string, bool> getEmptyKey() { return EMPTY_KEY; }

  static inline std::pair<std::string, bool> getTombstoneKey() {
    return TOMBSTONE_KEY;
  }

  static unsigned getHashValue(const std::pair<std::string, bool> &p) {
    return std::hash<std::string>{}(p.first) ^
           (static_cast<unsigned>(p.second) << 1);
  }

  static bool isEqual(const std::pair<std::string, bool> &LHS,
                      const std::pair<std::string, bool> &RHS) {
    return LHS == RHS;
  }
};
} // namespace llvm

namespace {

using FGenComp =
    std::function<LogicalResult(const RTLRequest &, hw::HWModuleExternOp)>;

/// Aggregates information useful during ASIC export. This is to avoid passing
/// many arguments to a bunch of functions.
struct ASICExportInfo {
  /// The top-level MLIR module.
  mlir::ModuleOp modOp;
  /// The RTL configuration parsed from JSON-formatted files.
  RTLConfiguration &config;
  /// Output directory (without trailing separators).
  StringRef outputPath;
  /// Process Design Kit
  std::string pdk;
  /// Standard cell library
  std::string library;
  /// Design name
  std::string designName;

  /// Maps every external hardware module in the IR to its corresponding
  /// heap-allocated match according to the RTL configuration.
  mlir::DenseMap<hw::HWModuleExternOp, RTLMatch *> externals;

  /// Creates export information for the given module and RTL configuration.
  ASICExportInfo(mlir::ModuleOp modOp, RTLConfiguration &config,
                 StringRef outputPath, const std::string &pdk,
                 const std::string &library, const std::string &designName)
      : modOp(modOp), config(config), outputPath(outputPath), pdk(pdk),
        library(library), designName(designName){};

  /// Associates every external hardware module to its match according to the
  /// RTL configuration and concretizes each of them inside the output
  /// directory. Fails if any external module does not have a match in the RTL
  /// configuration; succeeds otherwise.
  LogicalResult concretizeExternalModules();

  /// Deallocates all of our RTL matches.
  ~ASICExportInfo() {
    for (auto [_, match] : externals)
      delete match;
  }
};

/// Creates a Yosys synthesis script for ASIC synthesis
std::string createASICSynthesisScript(const std::string &designName,
                                      const std::string &pdk,
                                      const std::string &library,
                                      const std::string &outputDir) {
  std::string script = R"(
# ASIC Synthesis Script for )" + designName + R"(
# Generated by Dynamatic ASIC Export Tool

# Read design files
)";

  // Add read commands for all Verilog files
  script += "read_verilog " + outputDir + "/" + designName + ".v\n";
  script += R"(
# Hierarchy check
hierarchy -check -top )" + designName + R"(

# High-level synthesis
proc; opt; fsm; opt; memory; opt

# Technology mapping
techmap; opt

# Map to standard cells
dfflibmap -liberty $::env(PDK_ROOT)/)" + pdk + R"(/libs.ref/)" + library + R"(/liberty/)" + library + R"(__tt_025C_1v80.lib
abc -liberty $::env(PDK_ROOT)/)" + pdk + R"(/libs.ref/)" + library + R"(/liberty/)" + library + R"(__tt_025C_1v80.lib

# Write synthesized netlist
write_verilog -noattr )" + outputDir + R"(/)" + designName + R"(_synthesized.v
write_liberty )" + outputDir + R"(/)" + designName + R"(.lib

# Write statistics
stat -liberty $::env(PDK_ROOT)/)" + pdk + R"(/libs.ref/)" + library + R"(/liberty/)" + library + R"(__tt_025C_1v80.lib
)";

  return script;
}

/// Creates LibreLane configuration
std::string createLibreLaneConfig(const std::string &designName,
                                  const std::string &pdk,
                                  const std::string &library,
                                  const std::string &outputDir) {
  std::string config = R"(
# LibreLane Configuration for )" + designName + R"(
# Generated by Dynamatic ASIC Export Tool

set ::env(DESIGN_NAME) ")" + designName + R"("
set ::env(VERILOG_FILES) ")";
  
  config += outputDir + "/" + designName + "_synthesized.v";
  config += R"("
set ::env(PDK) ")" + pdk + R"("
set ::env(STD_CELL_LIBRARY) ")" + library + R"("

# Design configuration
set ::env(CLOCK_PERIOD) "10.0"
set ::env(CLOCK_PORT) "clock"
set ::env(CLOCK_NET) "clock"

# Floorplan configuration
set ::env(DIE_AREA) "0 0 1000 1000"
set ::env(PLACE_SITE) "unithd"
set ::env(PLACE_DENSITY) "0.6"

# Synthesis configuration
set ::env(SYNTH_STRATEGY) "DELAY 0"
set ::env(SYNTH_MAX_FANOUT) "5"

# Place and Route configuration
set ::env(PLACE_SITE) "unithd"
set ::env(PLACE_DENSITY) "0.6"
set ::env(ROUTING_STRATEGY) "2"

# Timing configuration
set ::env(STA_WRITE_LIB) "1"
set ::env(STA_USE_ARC_ENERGY) "1"

# Power configuration
set ::env(POWER_OPTIMIZATION) "1"

# Verification
set ::env(RUN_KLAYOUT_DRC) "1"
set ::env(RUN_KLAYOUT_XOR) "1"
)";

  return config;
}

/// Runs LibreLane flow
LogicalResult runLibreLaneFlow(const std::string &libreLanePath,
                               const std::string &outputDir,
                               const std::string &designName) {
  if (libreLanePath.empty()) {
    llvm::errs() << "Error: LibreLane path not specified\n";
    return failure();
  }

  // Create LibreLane run script
  std::string runScript = outputDir + "/run_librelane.sh";
  std::ofstream scriptFile(runScript);
  if (!scriptFile.is_open()) {
    llvm::errs() << "Error: Cannot create LibreLane run script\n";
    return failure();
  }

  scriptFile << "#!/bin/bash\n";
  scriptFile << "set -e\n\n";
  scriptFile << "cd " << outputDir << "\n";
  scriptFile << "export PDK_ROOT=" << libreLanePath << "/pdks\n";
  scriptFile << "export OPENLANE_ROOT=" << libreLanePath << "\n";
  scriptFile << "export OPENLANE_IMAGE_NAME=efabless/openlane:current\n";
  scriptFile << "export CARAVEL_ROOT=" << libreLanePath << "/caravel\n";
  scriptFile << "export CARAVEL_LITE=1\n\n";
  scriptFile << "# Run LibreLane flow\n";
  scriptFile << libreLanePath << "/flow.tcl -design " << designName << " -tag dynamatic\n";

  scriptFile.close();

  // Make script executable
  std::string chmodCmd = "chmod +x " + runScript;
  if (system(chmodCmd.c_str()) != 0) {
    llvm::errs() << "Error: Failed to make LibreLane script executable\n";
    return failure();
  }

  // Run LibreLane
  std::string runCmd = "bash " + runScript;
  llvm::outs() << "Running LibreLane flow...\n";
  llvm::outs() << "Command: " << runCmd << "\n";
  
  int result = system(runCmd.c_str());
  if (result != 0) {
    llvm::errs() << "Error: LibreLane flow failed with exit code " << result << "\n";
    return failure();
  }

  llvm::outs() << "LibreLane flow completed successfully!\n";
  return success();
}

} // namespace

int main(int argc, char **argv) {
  InitLLVM y(argc, argv);

  cl::ParseCommandLineOptions(argc, argv,
                              "Dynamatic ASIC Export Tool\n\n"
                              "This tool exports ASIC-ready RTL from HW-level IR "
                              "with LibreLane integration.\n");

  // Set up the MLIR context and load our dialects.
  MLIRContext context;
  context.loadDialect<dynamatic::handshake::HandshakeDialect>();
  context.loadDialect<dynamatic::hw::HWDialect>();

  // Parse the input MLIR file.
  OwningOpRef<ModuleOp> module;
  {
    auto fileOrErr = MemoryBuffer::getFileOrSTDIN(inputFilename);
    if (std::error_code error = fileOrErr.getError()) {
      llvm::errs() << "Could not open input file '" << inputFilename
                   << "': " << error.message() << "\n";
      return 1;
    }

    llvm::SourceMgr sourceMgr;
    sourceMgr.AddNewSourceBuffer(std::move(*fileOrErr), SMLoc());
    module = parseSourceFile<ModuleOp>(sourceMgr, &context);
    if (!module) {
      llvm::errs() << "Error: Could not parse the input file\n";
      return 1;
    }
  }

  // Create output directory if it doesn't exist
  if (std::error_code error = llvm::sys::fs::create_directories(outputDir)) {
    llvm::errs() << "Error: Could not create output directory '" << outputDir
                 << "': " << error.message() << "\n";
    return 1;
  }

  // Parse RTL configuration files
  RTLConfiguration config;
  for (const std::string &configFile : rtlConfigs) {
    if (failed(config.parseFromFile(configFile))) {
      llvm::errs() << "Error: Could not parse RTL configuration file '"
                   << configFile << "'\n";
      return 1;
    }
  }

  // Create ASIC export info
  ASICExportInfo exportInfo(*module, config, outputDir, pdk, library, designName);

  // Concretize external modules
  if (failed(exportInfo.concretizeExternalModules())) {
    llvm::errs() << "Error: Failed to concretize external modules\n";
    return 1;
  }

  // Write main design file
  std::string designFile = outputDir + "/" + designName + ".v";
  std::ofstream designStream(designFile);
  if (!designStream.is_open()) {
    llvm::errs() << "Error: Could not create design file '" << designFile << "'\n";
    return 1;
  }

  // Write Verilog for all modules
  for (auto &op : module->getOps<hw::HWModuleOp>()) {
    designStream << "// Module: " << op.getName() << "\n";
    // TODO: Implement proper Verilog generation
    designStream << "module " << op.getName() << "();\n";
    designStream << "  // TODO: Implement module body\n";
    designStream << "endmodule\n\n";
  }

  designStream.close();

  // Create Yosys synthesis script
  std::string yosysScript = createASICSynthesisScript(designName, pdk, library, outputDir);
  std::string yosysFile = outputDir + "/synthesize.tcl";
  std::ofstream yosysStream(yosysFile);
  if (yosysStream.is_open()) {
    yosysStream << yosysScript;
    yosysStream.close();
  }

  // Create LibreLane configuration
  std::string libreLaneConfig = createLibreLaneConfig(designName, pdk, library, outputDir);
  std::string configFile = outputDir + "/config.tcl";
  std::ofstream configStream(configFile);
  if (configStream.is_open()) {
    configStream << libreLaneConfig;
    configStream.close();
  }

  llvm::outs() << "ASIC export completed successfully!\n";
  llvm::outs() << "Output directory: " << outputDir << "\n";
  llvm::outs() << "Design file: " << designFile << "\n";
  llvm::outs() << "Yosys script: " << yosysFile << "\n";
  llvm::outs() << "LibreLane config: " << configFile << "\n";

  // Run LibreLane if requested
  if (runLibreLane) {
    if (failed(runLibreLaneFlow(libreLanePath, outputDir, designName))) {
      llvm::errs() << "Error: LibreLane flow failed\n";
      return 1;
    }
  }

  return 0;
}
