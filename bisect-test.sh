#!/bin/bash

builtin cd ..
rm -rf build
python -m pip install -v -e .
if gtimeout 5 python -c "import hangup; hangup.foo('')"; then
    exit 1
else
    exit 0
fi
