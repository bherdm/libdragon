/**
 * @file ksemaphore.c
 * @author Giovanni Bajo <giovannibajo@gmail.com>
 * @brief Kernel Semaphore prmitive
 * @ingroup kernel
 * 
 */
#include "ksemaphore.h"
#include "interrupt.h"
#include "kernel_internal.h"

void ksemaphore_init(ksemaphore_t *sem, int value)
{
    kmutex_init(&sem->mutex, KMUTEX_STANDARD);
    kcond_init(&sem->cond);
    sem->count = value;
}

void ksemaphore_destroy(ksemaphore_t *sem)
{
    kcond_destroy(&sem->cond);
    kmutex_destroy(&sem->mutex);
}

/**
 * @brief Change the count, safely against ksemaphore_post_isr.
 *
 * The mutex keeps threads apart, but an interrupt handler posts without it:
 * a thread's read-modify-write of the count must not straddle that increment,
 * or one of the two changes is lost.
 */
static void sem_count_add(ksemaphore_t *sem, int delta)
{
    disable_interrupts();
    sem->count += delta;
    enable_interrupts();
}

void ksemaphore_wait(ksemaphore_t *sem)
{
    kmutex_lock(&sem->mutex);
    while (sem->count <= 0)
        kcond_wait(&sem->cond, &sem->mutex);
    sem_count_add(sem, -1);
    kmutex_unlock(&sem->mutex);
}

bool ksemaphore_try_wait(ksemaphore_t *sem, uint32_t ticks_)
{
    bool ret = false;
    int64_t ticks = ticks_;
    kmutex_lock(&sem->mutex);
    while (sem->count <= 0 && ticks > 0) {
        uint64_t now = get_ticks();
        if (!kcond_wait_timeout(&sem->cond, &sem->mutex, ticks))
            break;
        ticks -= get_ticks() - now;
    }
    if (sem->count > 0) {
        sem_count_add(sem, -1);
        ret = true;
    }
    kmutex_unlock(&sem->mutex);
    return ret;
}

void ksemaphore_post(ksemaphore_t *sem)
{
    kmutex_lock(&sem->mutex);
    sem_count_add(sem, 1);
    kcond_signal(&sem->cond);
    kmutex_unlock(&sem->mutex);
}

void ksemaphore_post_isr(ksemaphore_t *sem)
{
    assert(exception_is_running());

    sem->count++;
    __kcond_signal_isr(&sem->cond);
}
