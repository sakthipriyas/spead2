#!/bin/bash

builtin cd ..
python -m pip install -v -e .
gtimeout 5 python -c "import hangup; hangup.foo('')"
