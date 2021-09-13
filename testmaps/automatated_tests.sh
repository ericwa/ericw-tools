#!/bin/bash

set -x

qbsp -noverbose invalid_texture_axes.map || exit 1
light invalid_texture_axes.map || exit 1

