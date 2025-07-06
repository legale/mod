// uevent_internal.h

// --- Ссылки (threadsafe, но низкоуровневые) ---
void uevent_ref(uev_t *uev);  // atomic
int uevent_unref(uev_t *uev); // atomic

// --- Проверки (internal, не требуют синхронизации) ---
static inline bool internal_is_persist_timer(const uevent_t *ev) {
  return (ev->events & (UEV_PERSIST | UEV_TIMEOUT)) == (UEV_PERSIST | UEV_TIMEOUT);
}

static inline bool internal_is_fd_event(const uevent_t *ev) {
  return (ev->events & UEV_FD_EVENTS) != 0 && ev->fd >= 0;
}

// --- Атомарные действия (atomic, не требуют синхронизации) ---
static inline bool atomic_deactivate_fd(uevent_t *ev) {
  return atomic_exchange_explicit(&ev->active_fd, false, memory_order_acq_rel);
}
static inline bool atomic_deactivate_timer(uevent_t *ev) {
  return atomic_exchange_explicit(&ev->active_timer, false, memory_order_acq_rel);
}

// --- Требуют внешней синхронизации (unsafe) ---

// проверяет нужно ли удалить fd событие из epoll
static inline bool internal_should_auto_del_fd(const uevent_t *ev, short triggered_events) {
  return (triggered_events & UEV_FD_EVENTS) && ((ev->events & UEV_PERSIST) == 0);
}

// безопасный таймаут, чтобы исключить таймауты меньше 1 мс (такие таймауты приводят к вечному циклу обработки таймеров)
static inline int get_min_timeout_internal(int timeout) {
  return (timeout < 1) ? 1 : timeout;
}
