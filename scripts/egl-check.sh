#!/bin/bash
# Quick EGL capability check for Staticwall

eglinfo -B 2>/dev/null | grep -A 20 "Wayland platform:" | grep -E "(EGL|OpenGL ES)" | head -10
