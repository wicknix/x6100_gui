#!/bin/bash

cmake -B _build/ -DENABLE_TESTING=YES && \
cmake --build _build/ --target all -j10 && \
cd _build/ && ctest --output-on-failure


