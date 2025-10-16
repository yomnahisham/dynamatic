# ASIC Support in Dynamatic

This document describes the ASIC support features added to Dynamatic, enabling the compilation of C/C++ code to ASIC-ready RTL with LibreLane integration.

## Overview

Dynamatic now supports ASIC synthesis through integration with open-source tools:
- **Yosys**: Logic synthesis
- **LibreLane**: Complete ASIC flow (synthesis, place & route, timing, verification)
- **SkyWater 130nm PDK**: Open-source process design kit

## Features

### 1. ASIC RTL Configuration
- **File**: `data/rtl-config-asic-sky130.json`
- **Purpose**: Defines ASIC-specific RTL components with synthesis parameters
- **Supports**: SkyWater 130nm PDK with standard cell libraries

### 2. ASIC Export Tool
- **Tool**: `tools/export-asic/export-asic`
- **Purpose**: Exports ASIC-ready RTL from HW-level IR
- **Features**:
  - Yosys synthesis script generation
  - LibreLane configuration generation
  - Complete ASIC flow automation

### 3. ASIC Flow Manager
- **Script**: `tools/backend/asic-flow/asic_flow.py`
- **Purpose**: Python-based ASIC flow management
- **Features**:
  - Automated synthesis
  - LibreLane integration
  - Report generation

## Usage

### Basic ASIC Export

```bash
# Export ASIC RTL
./bin/export-asic input.mlir output_dir data/rtl-config-asic-sky130.json \
  --pdk sky130 \
  --library sky130_fd_sc_hd \
  --design-name my_design
```

### Complete ASIC Flow

```bash
# Run complete ASIC flow with LibreLane
./bin/export-asic input.mlir output_dir data/rtl-config-asic-sky130.json \
  --pdk sky130 \
  --library sky130_fd_sc_hd \
  --design-name my_design \
  --run-librelane \
  --librelane-path /path/to/librelane
```

### Python ASIC Flow

```bash
# Run ASIC flow using Python script
python3 tools/backend/asic-flow/asic_flow.py my_design design.v \
  --pdk sky130 \
  --library sky130_fd_sc_hd \
  --run-librelane \
  --librelane-path /path/to/librelane
```

## Example Workflow

### 1. Compile C to Dataflow Circuit

```bash
# Compile C code to MLIR
clang -emit-llvm -S examples/asic_example.c -o example.ll

# Convert to Dynamatic dataflow
./bin/dynamatic-opt example.ll \
  --cf-to-handshake \
  --handshake-materialize \
  --handshake-infer-basic-blocks \
  --handshake-insert-buffers \
  --handshake-optimize \
  --handshake-to-hw \
  -o example_hw.mlir
```

### 2. Export ASIC RTL

```bash
# Export to ASIC RTL
./bin/export-asic example_hw.mlir asic_output data/rtl-config-asic-sky130.json \
  --pdk sky130 \
  --library sky130_fd_sc_hd \
  --design-name asic_example
```

### 3. Run Complete ASIC Flow

```bash
# Run LibreLane flow
python3 tools/backend/asic-flow/asic_flow.py asic_example asic_output/asic_example.v \
  --pdk sky130 \
  --library sky130_fd_sc_hd \
  --run-librelane \
  --librelane-path /path/to/librelane
```

## Output Files

The ASIC flow generates several output files:

### Synthesis Outputs
- `{design_name}.v`: Synthesized Verilog netlist
- `{design_name}.lib`: Liberty timing library
- `synthesize.tcl`: Yosys synthesis script

### LibreLane Outputs
- `runs/dynamatic/results/final/gds/`: Final GDSII layout
- `runs/dynamatic/results/final/def/`: Final DEF layout
- `runs/dynamatic/logs/`: Complete flow logs

## Configuration

### PDK Support
Currently supported PDKs:
- **SkyWater 130nm**: `sky130` (default)
- **GlobalFoundries 180nm**: `gf180` (planned)

### Standard Cell Libraries
- **SkyWater 130nm**: `sky130_fd_sc_hd` (default)
- **SkyWater 130nm**: `sky130_fd_sc_hs` (high-speed)
- **SkyWater 130nm**: `sky130_fd_sc_ls` (low-power)

### Design Parameters
- **Clock Period**: 10.0 ns (configurable)
- **Die Area**: 1000x1000 μm (configurable)
- **Place Density**: 0.6 (configurable)

## Dependencies

### Required Tools
- **Yosys**: Logic synthesis
- **LibreLane**: ASIC flow
- **Python 3**: Flow management

### Installation

```bash
# Install Yosys
sudo apt-get install yosys

# Install LibreLane
git clone https://github.com/The-OpenROAD-Project/OpenLane.git
cd OpenLane
make

# Install Python dependencies
pip3 install -r requirements.txt
```

## Troubleshooting

### Common Issues

1. **Yosys not found**: Install Yosys and ensure it's in PATH
2. **LibreLane path error**: Verify LibreLane installation path
3. **PDK not found**: Ensure PDK is installed in LibreLane
4. **Synthesis errors**: Check Verilog syntax and constraints

### Debug Mode

```bash
# Enable verbose output
export DYNAMATIC_DEBUG=1

# Run with debug information
./bin/export-asic input.mlir output_dir config.json --verbose
```

## Future Enhancements

### Planned Features
- **Multiple PDK Support**: GF180, TSMC, etc.
- **Advanced Optimization**: Power, area, timing
- **Formal Verification**: Property checking
- **Custom Libraries**: User-defined standard cells

### Contributing
- Report issues on GitHub
- Submit pull requests
- Join the community discussions

## References

- [LibreLane Documentation](https://github.com/The-OpenROAD-Project/OpenLane)
- [SkyWater PDK](https://github.com/google/skywater-pdk)
- [Yosys Documentation](https://yosyshq.net/yosys/)
- [Dynamatic Documentation](https://epfl-lap.github.io/dynamatic/)
