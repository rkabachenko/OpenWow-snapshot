#pragma once

#include <cstdint>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace openwow::compiler {

#if defined(_MSC_VER)

template <typename T>
inline T AtomicExchange(T* target, T value) noexcept {
  static_assert(sizeof(T) == 4, "32-bit values only");
  return static_cast<T>(_InterlockedExchange(
      reinterpret_cast<volatile long*>(target), static_cast<long>(value)));
}

template <typename T>
inline void AtomicStore(T* target, T value) noexcept {
  static_assert(sizeof(T) == 4, "32-bit values only");
  (void)_InterlockedExchange(reinterpret_cast<volatile long*>(target),
                             static_cast<long>(value));
}

template <typename T>
inline T AtomicLoad(const T* target) noexcept {
  static_assert(sizeof(T) == 4, "32-bit values only");

  return static_cast<T>(_InterlockedOr(
      reinterpret_cast<volatile long*>(const_cast<T*>(target)), 0));
}

template <typename T>
inline T AtomicFetchAdd(T* target, T delta) noexcept {
  static_assert(sizeof(T) == 4, "32-bit values only");
  return static_cast<T>(_InterlockedExchangeAdd(
      reinterpret_cast<volatile long*>(target), static_cast<long>(delta)));
}

template <typename T>
inline T AtomicFetchSub(T* target, T delta) noexcept {
  static_assert(sizeof(T) == 4, "32-bit values only");
  return static_cast<T>(_InterlockedExchangeAdd(
      reinterpret_cast<volatile long*>(target), -static_cast<long>(delta)));
}

template <typename T>
inline T AtomicFetchAddSeqCst(T* target, T delta) noexcept {
  return AtomicFetchAdd(target, delta);
}

template <typename T>
inline T AtomicFetchSubSeqCst(T* target, T delta) noexcept {
  return AtomicFetchSub(target, delta);
}

#else

template <typename T>
inline T AtomicExchange(T* target, T value) noexcept {
  return __atomic_exchange_n(target, value, __ATOMIC_ACQ_REL);
}

template <typename T>
inline void AtomicStore(T* target, T value) noexcept {
  __atomic_store_n(target, value, __ATOMIC_RELEASE);
}

template <typename T>
inline T AtomicLoad(const T* target) noexcept {
  return __atomic_load_n(target, __ATOMIC_ACQUIRE);
}

template <typename T>
inline T AtomicFetchAdd(T* target, T delta) noexcept {
  return __atomic_fetch_add(target, delta, __ATOMIC_ACQ_REL);
}

template <typename T>
inline T AtomicFetchSub(T* target, T delta) noexcept {
  return __atomic_fetch_sub(target, delta, __ATOMIC_ACQ_REL);
}

template <typename T>
inline T AtomicFetchAddSeqCst(T* target, T delta) noexcept {
  return __atomic_fetch_add(target, delta, __ATOMIC_SEQ_CST);
}

template <typename T>
inline T AtomicFetchSubSeqCst(T* target, T delta) noexcept {
  return __atomic_fetch_sub(target, delta, __ATOMIC_SEQ_CST);
}

#endif

}
