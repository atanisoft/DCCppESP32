/**********************************************************************
ESP32 COMMAND STATION

COPYRIGHT (c) 2021 Mike Dunston

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  You should have received a copy of the GNU General Public License
  along with this program.  If not, see http://www.gnu.org/licenses
**********************************************************************/

#ifndef SPINLOCK_HXX_
#define SPINLOCK_HXX_

#include <soc/spinlock.h>

namespace esp32cs
{

class Spinlock
{
public:
  Spinlock()
  {
    spinlock_initialize(&lock_);
  }

  /// Locks the specific critical section.
  void lock()
  {
    spinlock_acquire(&lock_, SPINLOCK_WAIT_FOREVER);
  }
  /// Unlocks the specific critical section.
  void unlock()
  {
    spinlock_release(&lock_);
  }

private:
  /// Performs fine-grained spinloop locking.
  spinlock_t lock_;
};

/// Simple wrapper to acquire and release a spinlock_t.
class SpinlockHolder
{
public:
  /// Constructor. Grabs the spinlock_t as a side effect.
  ///
  /// @param parent the spinlock_t to hold.
  ///
  SpinlockHolder(Spinlock *parent) : parent_(parent)
  {
    parent_->lock();
  }

  /// Destructor. Releases the spinlock_t as a side effect.
  ~SpinlockHolder()
  {
    parent_->unlock();
  }

private:
  /// Parent mutex we are holding.
  Spinlock *parent_;
};

} // namespace esp32cs

#endif // SPINLOCK_HOLDER_HXX_