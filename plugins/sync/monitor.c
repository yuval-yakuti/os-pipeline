#include "monitor.h"

int monitor_init(monitor_t* monitor) {
    if (!monitor) return -1;
    if (pthread_mutex_init(&monitor->mutex, NULL) != 0) return -1;
    if (pthread_cond_init(&monitor->condition, NULL) != 0) {
        pthread_mutex_destroy(&monitor->mutex);
        return -1;
    }
    monitor->signaled = 0;
    return 0;
}

void monitor_destroy(monitor_t* monitor) {
    if (!monitor) return;
    pthread_cond_destroy(&monitor->condition);
    pthread_mutex_destroy(&monitor->mutex);
}

void monitor_signal(monitor_t* monitor) {
    if (!monitor) return;
    pthread_mutex_lock(&monitor->mutex);
    monitor->signaled = 1;
    pthread_cond_broadcast(&monitor->condition);
    pthread_mutex_unlock(&monitor->mutex);
}

void monitor_reset(monitor_t* monitor) {
    if (!monitor) return;
    pthread_mutex_lock(&monitor->mutex);
    monitor->signaled = 0;
    pthread_mutex_unlock(&monitor->mutex);
}

int monitor_wait(monitor_t* monitor) {
    if (!monitor) return -1;
    pthread_mutex_lock(&monitor->mutex);
    while (!monitor->signaled) {
        pthread_cond_wait(&monitor->condition, &monitor->mutex);
    }
    monitor->signaled = 0; /* auto reset so subsequent waiters block again */
    pthread_mutex_unlock(&monitor->mutex);
    return 0;
}

