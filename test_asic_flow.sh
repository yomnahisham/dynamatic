#!/bin/bash
# Test script for Dynamatic ASIC flow
# This script demonstrates the complete C to ASIC flow

set -e

echo "🚀 Dynamatic ASIC Flow Test"
echo "=========================="

# Check if required tools are available
echo "Checking dependencies..."

if ! command -v yosys &> /dev/null; then
    echo "❌ Yosys not found. Please install Yosys first."
    echo "   Ubuntu/Debian: sudo apt-get install yosys"
    exit 1
fi

if ! command -v python3 &> /dev/null; then
    echo "❌ Python3 not found. Please install Python3 first."
    exit 1
fi

echo "✅ Dependencies check passed"

# Create test directory
TEST_DIR="asic_test_$(date +%s)"
mkdir -p $TEST_DIR
cd $TEST_DIR

echo "📁 Created test directory: $TEST_DIR"

# Create a simple test design
echo "📝 Creating test design..."
cat > test_design.v << 'DESIGN_EOF'
// Simple test design for ASIC flow
module test_design (
    input wire clock,
    input wire reset,
    input wire [31:0] a,
    input wire [31:0] b,
    output reg [31:0] result
);

always @(posedge clock) begin
    if (reset) begin
        result <= 32'b0;
    end else begin
        result <= a + b;
    end
end

endmodule
DESIGN_EOF

echo "✅ Test design created"

# Test Yosys synthesis
echo "🔧 Testing Yosys synthesis..."

cat > synthesize.tcl << 'YOSYS_EOF'
# Yosys synthesis script for test design
read_verilog test_design.v
hierarchy -check -top test_design
proc; opt; fsm; opt; memory; opt
techmap; opt
write_verilog -noattr test_design_synthesized.v
stat
YOSYS_EOF

yosys -c synthesize.tcl

if [ -f "test_design_synthesized.v" ]; then
    echo "✅ Yosys synthesis completed successfully"
    echo "   Generated: test_design_synthesized.v"
else
    echo "❌ Yosys synthesis failed"
    exit 1
fi

# Test Python ASIC flow manager
echo "🐍 Testing Python ASIC flow manager..."

python3 ../tools/backend/asic-flow/asic_flow.py test_design test_design.v \
    --pdk sky130 \
    --library sky130_fd_sc_hd \
    --yosys-only

if [ $? -eq 0 ]; then
    echo "✅ Python ASIC flow manager test passed"
else
    echo "❌ Python ASIC flow manager test failed"
    exit 1
fi

# Test ASIC export tool (if built)
echo "🔨 Testing ASIC export tool..."

if [ -f "../bin/export-asic" ]; then
    echo "✅ ASIC export tool found"
    echo "   Tool: ../bin/export-asic"
else
    echo "⚠️  ASIC export tool not found (needs to be built)"
    echo "   Run: make export-asic"
fi

# Create summary
echo ""
echo "📊 Test Summary"
echo "==============="
echo "✅ Yosys synthesis: PASSED"
echo "✅ Python flow manager: PASSED"
echo "⚠️  ASIC export tool: $(if [ -f "../bin/export-asic" ]; then echo "AVAILABLE"; else echo "NEEDS BUILDING"; fi)"
echo ""
echo "🎉 Basic ASIC flow test completed successfully!"
echo ""
echo "Next steps:"
echo "1. Build Dynamatic: make -j$(nproc)"
echo "2. Build ASIC export tool: make export-asic"
echo "3. Install LibreLane for complete ASIC flow"
echo "4. Run full ASIC flow with real designs"
echo ""
echo "Test directory: $TEST_DIR"
echo "Generated files:"
ls -la

cd ..
echo ""
echo "🏁 Test completed! Check the $TEST_DIR directory for results."
