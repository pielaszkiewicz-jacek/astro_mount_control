#!/bin/sh

candump can0 | grep -v "can0  141   \[8\]  9C" | grep -v "can0  141   \[8\]  92" | grep -v "can0  142   \[8\]  9C" | grep -v "can0  142   \[8\]  92"
