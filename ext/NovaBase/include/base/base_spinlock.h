#pragma once

#include "base/base_util.h"
#include <atomic>

BEGIN_NOVA_NAMESPACE(base)

class SpinLock
{
public:
    SpinLock() = default;
    ~SpinLock() = default;

    void Lock();
    void Unlock();
    void Trylock();

    void lock() { Lock(); };
    void unlock() { Unlock(); };

private:
    std::atomic<bool> locked_{false};
};
using spin_t = std::atomic<bool>;

void spin_lock(spin_t *lk);
void spin_unlock(spin_t *lk);
void spin_trylock(spin_t *lk);

END_NOVA_NAMESPACE(base)
