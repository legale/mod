/* timeutil.c – минимальный, lock-free кеш текущей TZ
   требование: без мьютексов, максимально быстрый доступ              */

#include "timeutil.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ====== служебные макросы ====== */
#define MAX_TZDATA_SIZE 8192
#define MAX_TZ_NAME_LEN 64

/* ====== указатели на системные функции, можно переопределить ====== */
// static timeutil_get_time_fn_t get_time_ptr = clock_gettime;
// static timeutil_sleep_fn_t sleep_fn_ptr = nanosleep;
// static timeutil_log_fn_t log_hook_ptr = NULL;

/* ====== структуры TZif ====== */
typedef struct {
  uint32_t ttisgmtcnt, ttisstdcnt, leapcnt, timecnt, typecnt, charcnt;
} tzif_header_t;

typedef struct {
  int32_t standard_offset;         /* смещение зимой (UTC+X) */
  int32_t daylight_offset;         /* смещение летом        */
  int16_t summer_start_yday;       /* день-года начала DST  */
  int16_t winter_start_yday;       /* день-года конца DST   */
  uint8_t has_dst;                 /* 1 если DST действует  */
  uint8_t valid;                   /* 1 если парс удался    */
  char zone_name[MAX_TZ_NAME_LEN]; /* имя зоны */
  char posix_tail[128];            /* POSIX-строка из TZif  */
} parsed_tz_t;

/* ====== глобальный кеш (единственный) ====== */
static parsed_tz_t g_tz;                  /* актуальная зона          */
static _Atomic unsigned g_tz_version = 0; /* версия кеша (счетчик)    */

static struct timespec g_ts_real_mono_off;

/* ====== чтение big-endian ====== */
static inline uint32_t rd_be32(const uint8_t *p) {
  return (uint32_t)p[0] << 24 |
         (uint32_t)p[1] << 16 |
         (uint32_t)p[2] << 8 |
         (uint32_t)p[3];
}
static inline int32_t rd_be32s(const uint8_t *p) {
  return (int32_t)rd_be32(p);
}
static inline int64_t rd_be64s(const uint8_t *p) {
  uint64_t v =
      (uint64_t)p[0] << 56 | (uint64_t)p[1] << 48 |
      (uint64_t)p[2] << 40 | (uint64_t)p[3] << 32 |
      (uint64_t)p[4] << 24 | (uint64_t)p[5] << 16 |
      (uint64_t)p[6] << 8 | (uint64_t)p[7];
  if (v & (1ull << 63)) /* знак */
    return (int64_t)(v - (1ull << 63)) + INT64_MIN;
  return (int64_t)v;
}

// сложение и вычитание
static inline void timespec_add(struct timespec *a, const struct timespec *b) {
  a->tv_sec += b->tv_sec;
  a->tv_nsec += b->tv_nsec;
  if (a->tv_nsec >= NS_PER_SEC) {
    ++a->tv_sec;
    a->tv_nsec -= NS_PER_SEC;
  }
}

static inline void timespec_sub(const struct timespec *a, const struct timespec *b, struct timespec *res) {
  res->tv_sec = a->tv_sec - b->tv_sec;
  res->tv_nsec = a->tv_nsec - b->tv_nsec;
  if (res->tv_nsec < 0) {
    --res->tv_sec;
    res->tv_nsec += NS_PER_SEC;
  }
}

/* ====== парсер TZif v2/v3 ====== */
static int parse_tzdata(const uint8_t *buf, size_t sz, parsed_tz_t *out) {
  // clang-format off
  if (sz < 44 || memcmp(buf, "TZif", 4)) return -1;
    const uint8_t *p = buf + 20;
    tzif_header_t h;
    h.ttisgmtcnt = rd_be32(p); p += 4;
    h.ttisstdcnt = rd_be32(p); p += 4;
    h.leapcnt    = rd_be32(p); p += 4;
    h.timecnt    = rd_be32(p); p += 4;
    h.typecnt    = rd_be32(p); p += 4;
    h.charcnt    = rd_be32(p); p += 4;
    if (!h.timecnt || !h.typecnt) return -1;
    size_t skip =
        h.timecnt * 4 + h.timecnt +
        h.typecnt * 6 + h.charcnt +
        h.leapcnt * 8 + h.ttisstdcnt + h.ttisgmtcnt;
    if ((p - buf) + skip > sz) return -1;
    p += skip;                         /* -> начало 64-битовой секции */

    if (memcmp(p, "TZif", 4)) return -1;
    p += 20;
    h.ttisgmtcnt = rd_be32(p); p += 4;
    h.ttisstdcnt = rd_be32(p); p += 4;
    h.leapcnt    = rd_be32(p); p += 4;
    h.timecnt    = rd_be32(p); p += 4;
    h.typecnt    = rd_be32(p); p += 4;
    h.charcnt    = rd_be32(p); p += 4;
    if (!h.timecnt || !h.typecnt) return -1;

    size_t blk =
        h.timecnt * 8 + h.timecnt +
        h.typecnt * 6 + h.charcnt +
        h.leapcnt * 12 + h.ttisstdcnt + h.ttisgmtcnt;
    if ((p - buf) + blk > sz) return -1;
  // clang-format on

  const uint8_t *ttimes = p;
  const uint8_t *types = p + h.timecnt * 8;
  const uint8_t *ttinfo = types + h.timecnt;

  int last_sum = -1, last_win = -1;
  int32_t off_sum = 0, off_win = 0;
  uint8_t saw_dst = 0;

  for (size_t i = 0; i < h.timecnt; ++i) {
    int64_t t = rd_be64s(ttimes + i * 8);
    uint8_t idx = types[i];
    if (idx >= h.typecnt) continue;
    int32_t off = rd_be32s(ttinfo + idx * 6);
    uint8_t isdst = ttinfo[idx * 6 + 4];

    struct tm tmv;
    if (!gmtime_r((time_t *)&t, &tmv)) continue;

    if (isdst) {
      saw_dst = 1;
      last_sum = tmv.tm_yday;
      off_sum = off;
    } else {
      last_win = tmv.tm_yday;
      off_win = off;
    }
  }

  memset(out, 0, sizeof(*out));
  out->standard_offset = off_win;
  out->daylight_offset = saw_dst ? off_sum : off_win;
  out->summer_start_yday = saw_dst ? last_sum : -1;
  out->winter_start_yday = saw_dst ? last_win : -1;
  out->has_dst = saw_dst;
  out->valid = 1;
  out->posix_tail[0] = '\0';

  /* --- POSIX-хвост --------------------------------------------------
     Формат: '\n' TEXT '\n'. Берём TEXT до следующего '\n' или EOF.
     Если в строке есть ',', значит зона использует DST.               */
  {
    const uint8_t *tail = p + blk;
    if (tail < buf + sz && tail[0] == '\n') {
      const char *posix = (const char *)(tail + 1); /* начало текста */
      size_t avail = (buf + sz) - (tail + 1);       /* доступный объём */
      const char *nl = memchr(posix, '\n', avail);  /* ищем конец      */
      size_t n = nl ? (size_t)(nl - posix) : avail; /* длина текста   */

      if (n >= sizeof(out->posix_tail)) /* усечение       */
        n = sizeof(out->posix_tail) - 1;

      memcpy(out->posix_tail, posix, n);
      out->posix_tail[n] = '\0';
      out->has_dst = strchr(out->posix_tail, ',') != NULL; /* ',' ⇒ DST      */
    }
  }
  return 0;
}

/* ====== загрузка файла зоны ====== */
static int load_and_parse_tz(const char *name, parsed_tz_t *dst) {
  static const char *paths[] = {
      "/usr/share/zoneinfo/", "/usr/lib/zoneinfo/", "/etc/zoneinfo/", NULL};
  char full[256];
  uint8_t buf[MAX_TZDATA_SIZE];
  int fd = -1;

  for (int i = 0; paths[i]; ++i) {
    snprintf(full, sizeof(full), "%s%s", paths[i], name);
    if ((fd = open(full, O_RDONLY)) >= 0) break;
  }
  if (fd < 0) return -1;

  struct stat st;
  if (fstat(fd, &st) < 0 || st.st_size > MAX_TZDATA_SIZE) {
    close(fd);
    return -1;
  }
  ssize_t n = read(fd, buf, st.st_size);
  close(fd);
  if (n != st.st_size) return -1;

  if (parse_tzdata(buf, n, dst) == 0) {
    strncpy(dst->zone_name, name, MAX_TZ_NAME_LEN - 1);
    dst->zone_name[MAX_TZ_NAME_LEN - 1] = '\0';
    return 0;
  }
  return -1;
}

/* ====== имя системной TZ ====== */
static int get_system_tz_name(char *dst, size_t cap) {
  const char *tz = getenv("TZ");
  if (tz) {
    strncpy(dst, tz, cap - 1);
    dst[cap - 1] = '\0';
    return 0;
  }

  FILE *f = fopen("/etc/timezone", "r");
  if (f) {
    if (fgets(dst, cap, f)) {
      char *nl = strchr(dst, '\n');
      if (nl) *nl = '\0';
      fclose(f);
      return 0;
    }
    fclose(f);
  }
  strncpy(dst, "UTC", cap);
  dst[cap - 1] = '\0';
  return 0;
}

/* ====== быстрый расчёт offset по кэшу ====== */
static inline int32_t fast_calc_offset(const parsed_tz_t *tz, time_t t) {
  if (!likely(tz->has_dst)) return tz->standard_offset;

  struct tm tmv;
  gmtime_r(&t, &tmv);
  int yday = tmv.tm_yday;

  if (tz->summer_start_yday < tz->winter_start_yday) {
    /* северное полушарие */
    if (yday >= tz->summer_start_yday && yday < tz->winter_start_yday) return tz->daylight_offset;
    return tz->standard_offset;
  }
  /* южное полушарие */
  if (yday >= tz->summer_start_yday || yday < tz->winter_start_yday) return tz->daylight_offset;
  return tz->standard_offset;
}

/* ====== публичные функции ====== */

/* один вызов – перечитать системную TZ и обновить кеш    */
void tu_update_offset(void) {
  char name[MAX_TZ_NAME_LEN];
  if (get_system_tz_name(name, sizeof(name)) != 0) return;

  /* если имя не поменялось и кеш валиден, ничего не делаем */
  if (g_tz.valid && strcmp(name, g_tz.zone_name) == 0) return;

  parsed_tz_t tmp;
  if (load_and_parse_tz(name, &tmp) == 0) {
    /* публикация новым номером версии */
    atomic_thread_fence(memory_order_release);
    g_tz = tmp;
    atomic_fetch_add_explicit(&g_tz_version, 1, memory_order_release);
  }
}

void tu_update_mono_real_offset() {
  struct timespec ts, ts2;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
  clock_gettime(CLOCK_REALTIME, &ts2);
  timespec_sub(&ts2, &ts, &g_ts_real_mono_off);
}

/* инициализация – парсим один раз */
void tu_init() {
  tu_update_offset();
  tu_update_mono_real_offset();
}

/* вернуть текущее (зимнее) смещение UTC */
int64_t tu_get_cached_tz_off() {
  return g_tz.standard_offset;
}

uint64_t tu_clock_gettime_monotonic_ms(void) {
  struct timespec ts;
  if (unlikely(clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)) return 0;
  return (uint64_t)ts.tv_sec * MS_PER_SEC + ts.tv_nsec / NS_PER_MS;
}

/* локальное время */
int tu_clock_gettime_local(struct timespec *ts) {
  if (unlikely(clock_gettime(CLOCK_REALTIME, ts) != 0)) return EINVAL;

  // делаем копию кеша (кеш меняется очень редко, допускаем гонку)
  parsed_tz_t tz = g_tz; // одно целостное копирование структуры

  // считаем offset по локальной копии
  time_t off = fast_calc_offset(&tz, ts->tv_sec);
  ts->tv_sec += off;

  return 0;
}

/* локальное время на основе MONOTONIC_RAW + оффсет */
int tu_clock_gettime_local_mono(struct timespec *ts) {
  if (unlikely(clock_gettime(CLOCK_MONOTONIC_RAW, ts) != 0)) return EINVAL;

  // добавляем оффсет, записанный в глобальную структуру
  ts->tv_sec += g_ts_real_mono_off.tv_sec;
  ts->tv_nsec += g_ts_real_mono_off.tv_nsec;

  // нормализация nsec, чтобы значение там оставалось меньше секунды
  if (ts->tv_nsec >= NS_PER_SEC) {
    ts->tv_nsec -= NS_PER_SEC;
    ts->tv_sec += 1;
  }

  // делаем копию кеша (кеш меняется очень редко, допускаем гонку)
  parsed_tz_t tz = g_tz; // одно целостное копирование структуры

  // сразу считаем offset по локальной копии
  ts->tv_sec += fast_calc_offset(&tz, ts->tv_sec);

  return 0;
}

/* ====== утилиты сна, timespec арифметика ====== */
int msleep(uint64_t ms) {
  struct timespec ts = {.tv_sec = ms / MS_PER_SEC, .tv_nsec = (ms % MS_PER_SEC) * NS_PER_MS};
  return nanosleep(&ts, NULL);
}

/* ====== атомарные helpers ====== */
void atomic_ts_load(atomic_timespec_t *src, struct timespec *dst) {
  dst->tv_sec = atomic_load_explicit(&src->tv_sec, memory_order_relaxed);
  dst->tv_nsec = atomic_load_explicit(&src->tv_nsec, memory_order_relaxed);
}
void atomic_ts_store(atomic_timespec_t *dst, struct timespec *src) {
  atomic_store_explicit(&dst->tv_sec, src->tv_sec, memory_order_relaxed);
  atomic_store_explicit(&dst->tv_nsec, src->tv_nsec, memory_order_relaxed);
}
void atomic_ts_cpy(atomic_timespec_t *dst, atomic_timespec_t *src) {
  struct timespec tmp;
  atomic_ts_load(src, &tmp);
  atomic_ts_store(dst, &tmp);
}
