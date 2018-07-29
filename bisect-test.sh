#!/bin/bash
function timeout() { perl -e 'alarm shift; exec @ARGV' "$@"; }

builtin cd ..
python -m pip install -v -e .
timeout 5 python -c "import hangup; hangup.foo('')"
