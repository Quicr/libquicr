#!/bin/sh
# SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

# Build at a known-good commit
echo "In update script"

if [ -d "./contrib/picoquic" ]
then
  echo "changing commit for picoquic"
  cd contrib/picoquic
  git checkout "suhas"
  cd ../..
else
  echo "NOT changing commit for picoquic" 
fi

if [ -d "./contrib/qproto" ]
then
  echo "changing commit for qproto"
  cd contrib/qproto
  git checkout "suhas"
  cd ../..
else
  echo "NOT changing commit for qproto"
fi
