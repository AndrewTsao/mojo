// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SKY_TOOLS_TESTER_TEST_RUNNER_H_
#define SKY_TOOLS_TESTER_TEST_RUNNER_H_

#include "base/memory/weak_ptr.h"
#include "sky/tools/tester/test_harness_impl.h"

namespace mojo{
class View;
}

namespace sky {
namespace tester {

class TestRunnerClient {
 public:
  virtual void OnTestComplete() = 0;
  virtual void DispatchInputEvent(mojo::EventPtr event) = 0;

 protected:
  virtual ~TestRunnerClient();
};

class TestRunner {
 public:
  TestRunner(TestRunnerClient* client, mojo::View* container,
      const std::string& url);
  virtual ~TestRunner();

  TestRunnerClient* client() const { return client_; }
  TestHarnessFactory* test_harness_factory() { return &test_harness_factory_; }

  base::WeakPtr<TestRunner> GetWeakPtr();
  void OnTestStart();
  void OnTestComplete(const std::string& test_result);

 private:
  TestHarnessFactory test_harness_factory_;
  TestRunnerClient* client_;
  base::WeakPtrFactory<TestRunner> weak_ptr_factory_;

  MOJO_DISALLOW_COPY_AND_ASSIGN(TestRunner);
};

}  // namespace tester
}  // namespace sky

#endif  // SKY_TOOLS_TESTER_TEST_RUNNER_H_
