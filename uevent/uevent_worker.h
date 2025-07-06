#ifndef LIBUEVENT_UEVENT_WORKER_H
#define LIBUEVENT_UEVENT_WORKER_H

#include <stdbool.h>
#include <stdint.h>

// Forward-declaration для uevent_t, чтобы избежать циклической зависимости,
// так как uevent.h будет включать этот файл.
typedef struct uevent_t uevent_t;

/**
 * @brief Непрозрачный тип для пула рабочих потоков.
 *
 * Детали реализации скрыты в uevent_worker.c.
 */
typedef struct uevent_worker_pool_t uevent_worker_pool_t;

/** Control creation of temporary extra workers. Useful for tests. */
void uevent_worker_pool_enable_extra_workers(bool enable);

/**
 * @brief Создает и запускает пул рабочих потоков.
 *
 * @param num_workers Количество потоков, которые нужно создать в пуле.
 * @return Указатель на созданный пул или NULL в случае ошибки.
 */
uevent_worker_pool_t *uevent_worker_pool_create(int num_workers);

/**
 * @brief Помещает задачу (вызов колбэка) в очередь на выполнение.
 *
 * Функция потокобезопасна. Один из свободных потоков в пуле
 * заберет задачу и выполнит ее.
 *
 * @param pool Указатель на пул рабочих потоков.
 * @param ev Указатель на событие, которое вызвало колбэк.
 * @param triggered_events Флаги сработавших событий (UEV_READ, UEV_TIMEOUT и т.д.).
 */
void uevent_worker_pool_insert(uevent_worker_pool_t *pool, uev_t *uev, short triggered_events, uint64_t cron_time);

/**
 * @brief Корректно останавливает и уничтожает пул рабочих потоков.
 *
 * Функция дожидается завершения всех потоков и освобождает все
 * связанные с пулом ресурсы.
 *
 * @param pool Указатель на пул, который нужно уничтожить.
 */
void uevent_worker_pool_destroy(uevent_worker_pool_t *pool);

/**
 * @brief Проверяет, завершил ли пул все свои задачи.
 * @return true, если очередь задач пуста и ни один воркер не занят.
 */
bool uevent_worker_pool_is_idle(uevent_worker_pool_t *pool);

/**
 * @brief Блокирует вызывающий поток до тех пор, пока пул не станет свободным.
 */
void uevent_worker_pool_wait_for_idle(uevent_worker_pool_t *pool);

void uevent_worker_pool_stop(uevent_worker_pool_t *pool);

#endif /* LIBUEVENT_UEVENT_WORKER_H */
