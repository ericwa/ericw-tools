#!/bin/bash

set -x

for MAP in *.MAP; do
  qbsp -leaktest "$MAP" || exit 1
done

