// Copyright 2022 Huawei Cloud Computing Technology Co., Ltd.
// Copyright 2026 The jst-build authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ATOMIC_VALUE_HPP
#define INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ATOMIC_VALUE_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <tuple>

// Value that can be set and get atomically. Reset is not thread-safe.
// Assignment operators will not overwrite existing values.
// Once set, the value is published via an atomic raw pointer, so reading it
// does neither take a lock nor touch a reference count (the value is owned by
// a shared pointer that is written before publication and never afterwards).
template <class T>
class AtomicValue {
  public:
    AtomicValue() noexcept = default;
    AtomicValue(AtomicValue const& other) noexcept { CopyFrom(other); }
    AtomicValue(AtomicValue&& other) noexcept { CopyFrom(other); }
    ~AtomicValue() noexcept = default;

    auto operator=(AtomicValue const& other) noexcept -> AtomicValue& {
        if (other.ptr_.load() != nullptr) {
            auto owner = other.owner_;
            std::ignore = SetOnceAndGet(std::function<std::shared_ptr<T>()>{
                [&owner]() { return owner; }});
        }
        return *this;
    }
    auto operator=(AtomicValue&& other) noexcept -> AtomicValue& {
        *this = static_cast<AtomicValue const&>(other);
        return *this;
    }

    // Atomically set value once and return its reference. If this method is
    // called multiple times concurrently, the setter is called only once. In
    // any case, this method blocks until the value is ready.
    [[nodiscard]] auto SetOnceAndGet(
        std::function<T()> const& setter) const& noexcept -> T const& {
        auto const* value = ptr_.load();
        if (value == nullptr) {
            if (not load_.exchange(true)) {
                owner_ = std::make_shared<T>(setter());
                ptr_.store(owner_.get());
                ptr_.notify_all();
                return *owner_;
            }
            ptr_.wait(nullptr);
            return *ptr_.load();
        }
        return *value;
    }

    [[nodiscard]] auto SetOnceAndGet(std::function<std::shared_ptr<T>()> const&
                                         setter) const& noexcept -> T const& {
        auto const* value = ptr_.load();
        if (value == nullptr) {
            if (not load_.exchange(true)) {
                owner_ = setter();
                ptr_.store(owner_.get());
                ptr_.notify_all();
                return *owner_;
            }
            ptr_.wait(nullptr);
            return *ptr_.load();
        }
        return *value;
    }

    [[nodiscard]] auto SetOnceAndGet(std::function<T()> const& setter) && =
        delete;

    // The value if set, nullptr otherwise.
    [[nodiscard]] auto TryGet() const noexcept -> T const* {
        return ptr_.load();
    }

    // Reset, not thread-safe!
    void Reset() noexcept {
        load_ = false;
        ptr_ = nullptr;
        owner_.reset();
    }

  private:
    mutable std::atomic<bool> load_{false};
    mutable std::atomic<T const*> ptr_{nullptr};
    mutable std::shared_ptr<T> owner_{};

    void CopyFrom(AtomicValue const& other) noexcept {
        if (other.ptr_.load() != nullptr) {
            owner_ = other.owner_;  // published, not written anymore
            load_ = true;
            ptr_.store(owner_.get());
        }
    }
};

#endif  // INCLUDED_SRC_BUILDTOOL_MULTITHREADING_ATOMIC_VALUE_HPP
