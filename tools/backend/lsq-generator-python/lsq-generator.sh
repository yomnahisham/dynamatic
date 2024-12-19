#!/bin/bash

DYNAMATIC=$1
OUTPUT_DIR=$2
MODULE_NAME=$3

source $DYNAMATIC/tools/dynamatic/scripts/utils.sh
source $DYNAMATIC/tools/specify-python.sh

echo_info "Using tools/specify-python.sh to specify python executable"

$DYNAMATIC_PYTHON_BIN $DYNAMATIC/tools/backend/lsq-generator-python/lsq-generator.py \
  -o $OUTPUT_DIR -c $OUTPUT_DIR/$MODULE_NAME.json

