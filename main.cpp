#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <new>
#include <numeric>
#include <random>
#include <thread>
#include <vector>
#ifdef __linux__
#include <sched.h>
#include <stdexcept>
#endif

constexpr std::size_t BUFFER_SIZE = 2 * 1024 * 1024;

const std::size_t DISTANCES[] = {8, 16, 32, 64, 128, 256};
constexpr std::size_t BLOCK_SIZE = 512;


constexpr std::size_t SIZES[] = {4 * 1024,  8 * 1024,   12 * 1024,  16 * 1024,
                                 20 * 1024, 24 * 1024,  28 * 1024,  32 * 1024,
                                 36 * 1024, 40 * 1024,  48 * 1024,  64 * 1024,
                                 96 * 1024, 128 * 1024, 192 * 1024, 256 * 1024};
const std::size_t COUNTS[] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 16, 32};
constexpr std::size_t SET_STRIDE = 4096;


constexpr std::size_t ITERATIONS = 100'000;
constexpr std::size_t MEASURE_ROUNDS = 10;
constexpr std::size_t WARMUP_ITERATIONS = 100'000;

static std::mt19937 rng(692026);
static char *volatile sink;

namespace {
#ifdef __linux__
    void pin_to_cpu(const int cpu) {
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        CPU_SET(cpu, &cpu_set);

        if (sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set) != 0) {
            throw std::runtime_error("Failed to pin CPU");
        }
    }
#endif

    std::size_t find_jump(const std::vector<double> &values,
                          const double min_ratio = 1.1) {
        for (std::size_t i = 1; i < values.size(); ++i) {
            if (const double ratio = values[i] / values[i - 1]; ratio >= min_ratio) {
                return i;
            }
        }

        return 0;
    }

    void build_chain(const std::vector<char *> &addresses) {
        for (std::size_t i = 0; i < addresses.size(); ++i) {
            *reinterpret_cast<char **>(addresses[i]) =
                    addresses[(i + 1) % addresses.size()];
        }
    }

    double measure_chain_once(char **ptr) {
        const auto start_time = std::chrono::steady_clock::now();

        for (std::size_t i = 0; i < ITERATIONS; ++i) {
            ptr = reinterpret_cast<char **>(*ptr);
        }

        sink = reinterpret_cast<char *>(ptr);

        const auto end_time = std::chrono::steady_clock::now();

        return std::chrono::duration<double, std::nano>(end_time - start_time).count() /
               ITERATIONS;
    }

    double measure_chain(char **ptr) {
        char **ptr_warm = ptr;
        for (std::size_t i = 0; i < WARMUP_ITERATIONS; ++i) {
            ptr_warm = reinterpret_cast<char **>(*ptr_warm);
        }
        sink = reinterpret_cast<char *>(ptr_warm);

        std::vector<double> measurements;
        measurements.reserve(MEASURE_ROUNDS);

        for (std::size_t i = 0; i < MEASURE_ROUNDS; ++i) {
            measurements.push_back(measure_chain_once(ptr));
        }

        std::ranges::sort(measurements);

        return measurements[0];
    }


    char *allocate_buffer() {
        const auto buffer = static_cast<char *>(std::aligned_alloc(4096, BUFFER_SIZE));

        if (!buffer) {
            throw std::bad_alloc();
        }

        return buffer;
    }
} // namespace

/* Определяем длинну кэш-линии.
 * Для каждого теста берём пары адресов P и Q, находящиеся на расстоянии d друг от друга.
 *
 * 1) d < cache_line_size => оба адреса находятся в одной линии кэша, обращение остаётся
 * дешёвым
 * 2) d >= cache_line_size => Q в другой линии и среднее время обращения увеличивается.
 *
 * Ищем самый большой скачок времени.
 */
static std::size_t detect_line() {
    char *buffer = allocate_buffer();

    std::vector<std::size_t> blocks(2048);
    std::iota(blocks.begin(), blocks.end(), 0);
    std::ranges::shuffle(blocks, rng);
    std::vector<double> times;

    for (const auto d: DISTANCES) {
        std::vector<char *> addresses;
        addresses.reserve(blocks.size() * 2);

        for (const auto block: blocks) {
            char *p = buffer + block * BLOCK_SIZE + BLOCK_SIZE - sizeof(char *);
            char *q = p - d;

            addresses.push_back(p);
            addresses.push_back(q);
        }

        build_chain(addresses);

        const auto ptr = reinterpret_cast<char **>(addresses[0]);
        const double access_time_ns = measure_chain(ptr);

#ifdef DEBUG
        std::cout << d << "B -> " << access_time_ns << " ns\n";
#endif

        times.push_back(access_time_ns);
    }

#ifdef DEBUG
    std::cout << "---------------------------\n";
#endif

    std::free(buffer);

    if (const std::size_t jump = find_jump(times); jump > 0) {
        return DISTANCES[jump];
    }

    return 0;
}

/* Определяем объём L1-кэша.
 * Для каждого теста задаём size — объём памяти, к которому обращаемся.
 * Берём по одному адресу из каждой кэш-линии.
 *
 * 1) size <= cache_size => данные помещаются в L1, обращения остаются быстрыми.
 * 2) size > cache_size => данные перестают помещаться в L1, время доступа увеличивается.
 *
 * Ищем самый большой скачок времени.
 */
static std::size_t detect_capacity(const std::size_t cache_line_size) {
    char *buffer = allocate_buffer();

    std::vector<double> times;

    for (const auto size: SIZES) {
        const std::size_t lines = size / cache_line_size;

        std::vector<std::size_t> order(lines);

        std::iota(order.begin(), order.end(), 0);
        std::ranges::shuffle(order, rng);
        std::vector<char *> addresses;
        addresses.reserve(lines);

        for (const auto line: order) {
            addresses.push_back(buffer + line * cache_line_size);
        }

        build_chain(addresses);
        const auto ptr = reinterpret_cast<char **>(addresses[0]);
        const double access_time_ns = measure_chain(ptr);

        times.push_back(access_time_ns);

#ifdef DEBUG
        std::cout << size / 1024 << " KB -> " << access_time_ns << " ns\n";
#endif
    }
#ifdef DEBUG
    std::cout << "---------------------------\n";
#endif
    std::free(buffer);

    if (const std::size_t jump = find_jump(times); jump > 0) {
        return SIZES[jump - 1] / 1024;
    }

    return 0;
}

/* Определяем ассоциативность L1-кэша
 * Для каждого теста берём count адресов с шагом SET_STRIDE, все они попадают в один
 * набор.
 *
 * 1) count <= associativity => все линии помещаются в набор, обращения остаются быстрыми.
 * 2) count > associativity => линии вытесняют друг друга, время доступа увеличивается.
 *
 * Ищем самый большой скачок времени
 */
static std::size_t detect_associativity() {
    char *buffer = allocate_buffer();
    std::vector<double> times;

    for (const auto count: COUNTS) {
        std::vector<char *> addresses;
        addresses.reserve(count);

        for (std::size_t i = 0; i < count; ++i) {
            addresses.push_back(buffer + i * SET_STRIDE);
        }

        std::ranges::shuffle(addresses, rng);
        build_chain(addresses);

        const auto ptr = reinterpret_cast<char **>(addresses[0]);

        const double time = measure_chain(ptr);
        times.push_back(time);

#ifdef DEBUG
        std::cout << count << " lines -> " << time << " ns\n";
#endif
    }

#ifdef DEBUG
    std::cout << "---------------------------\n";
#endif
    std::free(buffer);

    if (const std::size_t jump = find_jump(times); jump > 0) {
        return COUNTS[jump - 1];
    }

    return 0;
}

int main() {
#ifdef __linux__
    pin_to_cpu(static_cast<int>(std::thread::hardware_concurrency()) - 1);
#endif

    auto cache_line_size = detect_line();

    while (cache_line_size == 0) {
#ifdef DEBUG
        std::cout << "Cache line not found\n";
#endif
        cache_line_size = detect_line();
    }

    const auto capacity = detect_capacity(cache_line_size);
    const auto ways = detect_associativity();

    if (cache_line_size != 0) {
        std::cout << "Cache line: " << cache_line_size << " B\n";
    } else {
        std::cout << "Cache line not found\n";
    }

    if (capacity != 0) {
        std::cout << "Cache capacity ~= " << capacity << " KB\n";
    } else {
        std::cout << "Cache capacity not found\n";
    }

    if (ways != 0) {
        std::cout << "Associativity ~= " << ways << "-way\n";
    } else {
        std::cout << "Associativity not found\n";
    }

    return 0;
}
