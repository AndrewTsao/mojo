# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Presubmit script for mojo

See http://dev.chromium.org/developers/how-tos/depottools/presubmit-scripts
for more details about the presubmit API built into depot_tools.
"""

import os.path

def CheckChangeOnUpload(input_api, output_api):
  # Additional python module paths (we're in src/mojo/); not everyone needs
  # them, but it's easiest to add them to everyone's path.
  # For ply and jinja2:
  third_party_path = os.path.join(
      input_api.PresubmitLocalPath(), "..", "third_party")
  # For the bindings generator:
  mojo_public_bindings_pylib_path = os.path.join(
      input_api.PresubmitLocalPath(), "public", "tools", "bindings", "pylib")
  # For the python bindings:
  mojo_python_bindings_path = os.path.join(
      input_api.PresubmitLocalPath(), "public", "python")
  # For the python bindings tests:
  mojo_python_bindings_tests_path = os.path.join(
      input_api.PresubmitLocalPath(), "python", "tests")
  # For the roll tools scripts:
  mojo_roll_tools_path = os.path.join(
      input_api.PresubmitLocalPath(), "tools", "roll")
  # For all mojo/tools scripts
  mopy_path = os.path.join(input_api.PresubmitLocalPath(), "tools")
  # TODO(vtl): Don't lint these files until the (many) problems are fixed
  # (possibly by deleting/rewriting some files).
  temporary_black_list = input_api.DEFAULT_BLACK_LIST + \
      (r".*\bpublic[\\\/]tools[\\\/]bindings[\\\/]pylib[\\\/]mojom[\\\/]"
           r"generate[\\\/].+\.py$",
       r".*\bpublic[\\\/]tools[\\\/]bindings[\\\/]generators[\\\/].+\.py$",
       r".*\bspy[\\\/]ui[\\\/].+\.py$")

  results = []
  pylint_extra_paths = [
      third_party_path,
      mojo_public_bindings_pylib_path,
      mojo_python_bindings_path,
      mojo_python_bindings_tests_path,
      mojo_roll_tools_path,
      mopy_path,
  ]
  results += input_api.canned_checks.RunPylint(
      input_api, output_api, extra_paths_list=pylint_extra_paths,
      black_list=temporary_black_list)
  return results
