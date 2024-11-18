#!/usr/bin/env bash
# Check for DONOTSUBMIT in the diff
if git diff --cached | grep -q "DONOTSUBMIT"; then
  echo "Error: Commit contains the forbidden word 'DONOTSUBMIT'. Please remove it before committing."
  exit 1
fi
