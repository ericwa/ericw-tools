#!/bin/bash

QBSP_EXE=~/dev/tyrutils/build-ninja/qbsp/qbsp
$QBSP_EXE testshadows.map
$QBSP_EXE testslime.map

./make_screenshot.sh shadows.jpg testshadows.map "" "" 0
./make_screenshot.sh slime.jpg testslime.map "-backend bsp" "" 0

