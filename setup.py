#!/usr/bin/env python

from setuptools import setup, Extension
import pybind11

setup(
    name='hangup',
    version='0.1',
    ext_modules=[Extension(
        name='hangup',
        sources=['hangup.cpp'],
        include_dirs=['pybind11/include'],
        language='c++',
        extra_compile_args=['-std=c++11'])])
