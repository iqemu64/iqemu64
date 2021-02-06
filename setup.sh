#!/bin/sh

set -eu

echo "NOTE: This script WILL backup and patch some of your Xcode binaries."

source functions.sh

fn_pre_check
fn_variables
fn_symbol_links

pushd deps
fn_iemukern
fn_libunwind64
fn_libcxx
fn_dllmain_pswitch
fn_HackedBinaries
popd
fn_iqemu64

echo "\n\nDone! Now you can install and run arm64 Apps in ${XcodePath}/Contents/Developer/Applications/Simulator.app"
