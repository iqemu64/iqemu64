#!/bin/sh

set -eu

source functions.sh

fn_pre_check
fn_variables

pushd deps
echo "Restoring Xcode....."
un_libunwind64
un_HackedBinaries
echo "Done!"
popd
