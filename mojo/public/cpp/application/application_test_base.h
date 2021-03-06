// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MOJO_PUBLIC_CPP_APPLICATION_APPLICATION_TEST_BASE_H_
#define MOJO_PUBLIC_CPP_APPLICATION_APPLICATION_TEST_BASE_H_

#include "mojo/public/cpp/bindings/array.h"
#include "mojo/public/cpp/bindings/string.h"
#include "mojo/public/cpp/system/macros.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {

class ApplicationDelegate;
class ApplicationImpl;

namespace test {

// Access the shell handle shared by multiple test application instances.
ScopedMessagePipeHandle PassShellHandle();
void SetShellHandle(ScopedMessagePipeHandle handle);

// Access the command line arguments passed to the application test.
const Array<String>& Args();
void InitializeArgs(int argc, std::vector<const char*> argv);

// A GTEST base class for application testing executed in mojo_shell.
class ApplicationTestBase : public testing::Test {
 public:
  ApplicationTestBase();
  ~ApplicationTestBase() override;

 protected:
  ApplicationImpl* application_impl() { return application_impl_; }

  // Get the ApplicationDelegate for the application to be tested.
  virtual ApplicationDelegate* GetApplicationDelegate() = 0;

  // A testing::Test::SetUp helper to override the application command
  // line arguments.
  void SetUpWithArgs(const Array<String>& args);

  // testing::Test:
  void SetUp() override;
  void TearDown() override;

 private:
  // The application implementation instance, reconstructed for each test.
  ApplicationImpl* application_impl_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(ApplicationTestBase);
};

}  // namespace test

}  // namespace mojo

#endif  // MOJO_PUBLIC_CPP_APPLICATION_APPLICATION_TEST_BASE_H_
