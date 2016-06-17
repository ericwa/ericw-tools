#!/bin/bash

~/dev/tyrutils/build-ninja/qbsp/qbsp testshadows.map

./make_screenshot.sh shadows.jpg testshadows.map "" "" 0
