#!/usr/bin/env python3
"""Inserts 4 minimal changes into the root CMakeLists.txt for the 'vita' backend.
Idempotent. Keeps a copy in CMakeLists.txt.orig. Respects CRLF."""
import sys, os, shutil
root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
path = os.path.join(root, "CMakeLists.txt")
raw = open(path, "rb").read().decode("utf-8")
crlf = "\r\n" in raw
s = raw.replace("\r\n", "\n")
if "vita/vita.cmake" in s:
    print("Already patched."); sys.exit(0)

def sub(old, new, label):
    global s
    if s.count(old) != 1:
        sys.exit(f"ERROR: anchor '{label}' found {s.count(old)} times (expected 1). Nothing was changed.")
    s = s.replace(old, new)

sub("project(umk3 C)\n",
    'project(umk3 C)\n\ninclude("${CMAKE_SOURCE_DIR}/vita/vita.cmake")\n', "project")

sub('if(WIN32)\n    set(UMK3_BACKEND "win32"',
    'if(VITA)\n    set(UMK3_BACKEND "vita" CACHE STRING "Platform backend: win32, sdl2 or vita" FORCE)\n'
    'elseif(WIN32)\n    set(UMK3_BACKEND "win32"', "backend")

sub('    else()\n        message(FATAL_ERROR "Unknown UMK3_BACKEND',
    '    elseif(UMK3_BACKEND STREQUAL "vita")\n        umk3_vita_link_platform(${target_name})\n'
    '    else()\n        message(FATAL_ERROR "Unknown UMK3_BACKEND', "link")

sub("umk3_target_link_platform(umk3-game TRUE)\n",
    'umk3_target_link_platform(umk3-game TRUE)\n'
    '            if(UMK3_BACKEND STREQUAL "vita")\n                umk3_vita_package(umk3-game)\n            endif()\n', "package")

shutil.copy(path, path + ".orig")
out = s.replace("\n", "\r\n") if crlf else s
open(path, "wb").write(out.encode("utf-8"))
print("CMakeLists.txt patched (original in CMakeLists.txt.orig)")
