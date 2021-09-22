#!/bin/bash

set -x

for file in *.map; do
  qbsp -leaktest -noverbose "$file" || exit 1
done

exit 0
