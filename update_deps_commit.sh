#!/bin/sh
#build last picotls master (for Travis)

# Build at a known-good commit
COMMIT_ID=7ff95968e783799236d1ad642c69a21e1e73e575
echo "In update script"
if [ -d "./contrib/qproto" ]
then
  echo "changing commit for qproto"
  cd contrib/qproto
  git checkout -q "$COMMIT_ID"
  cd ../..
else
  echo "NOT changing commit for qproto"
fi
