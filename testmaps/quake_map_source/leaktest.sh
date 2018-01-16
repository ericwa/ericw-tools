#!/bin/bash

set -x

for file in *.map; do
  qbsp -leaktest "$file" || exit 1
done

