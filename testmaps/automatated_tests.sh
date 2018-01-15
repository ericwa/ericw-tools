#!/bin/bash

set -x

qbsp invalid_texture_axes.map || exit 1
light invalid_texture_axes.map || exit 1

