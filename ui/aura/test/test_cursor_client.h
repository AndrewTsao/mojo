// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_AURA_TEST_TEST_CURSOR_CLIENT_H_
#define UI_AURA_TEST_TEST_CURSOR_CLIENT_H_

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "ui/aura/client/cursor_client.h"

namespace ui {
class KeyEvent;
}

namespace aura {
namespace test {

class TestCursorClient : public aura::client::CursorClient {
 public:
  explicit TestCursorClient(aura::Window* root_window);
  virtual ~TestCursorClient();

  // Used to track the number of times SetCursor() was called.
  int calls_to_set_cursor() const { return calls_to_set_cursor_; }
  void reset_calls_to_set_cursor() { calls_to_set_cursor_ = 0; }

  // Set whether or not to hide cursor on key events.
  void set_should_hide_cursor_on_key_event(bool hide) {
    should_hide_cursor_on_key_event_ = hide;
  }

  // Overridden from aura::client::CursorClient:
  virtual void SetCursor(gfx::NativeCursor cursor) override;
  virtual gfx::NativeCursor GetCursor() const override;
  virtual void ShowCursor() override;
  virtual void HideCursor() override;
  virtual void SetCursorSet(ui::CursorSetType cursor_set) override;
  virtual ui::CursorSetType GetCursorSet() const override;
  virtual bool IsCursorVisible() const override;
  virtual void EnableMouseEvents() override;
  virtual void DisableMouseEvents() override;
  virtual bool IsMouseEventsEnabled() const override;
  virtual void SetDisplay(const gfx::Display& display) override;
  virtual void LockCursor() override;
  virtual void UnlockCursor() override;
  virtual bool IsCursorLocked() const override;
  virtual void AddObserver(
      aura::client::CursorClientObserver* observer) override;
  virtual void RemoveObserver(
      aura::client::CursorClientObserver* observer) override;
  virtual bool ShouldHideCursorOnKeyEvent(
      const ui::KeyEvent& event) const override;

 private:
  bool visible_;
  bool should_hide_cursor_on_key_event_;
  bool mouse_events_enabled_;
  int cursor_lock_count_;
  int calls_to_set_cursor_;
  ObserverList<aura::client::CursorClientObserver> observers_;
  aura::Window* root_window_;

  DISALLOW_COPY_AND_ASSIGN(TestCursorClient);
};

}  // namespace test
}  // namespace aura

#endif // UI_AURA_TEST_TEST_CURSOR_CLIENT_H_
