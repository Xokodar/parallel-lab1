#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma execution_character_set("utf-8")
#ifndef HAVE_STRUCT_TIMESPEC
#define HAVE_STRUCT_TIMESPEC
#endif
#pragma comment(lib, "pthreadVCE2.lib")
#endif

#include "pthread.h"

using namespace std;

const double a = 5.0;
const double b = 15.0;
const double exact = 0.03598268362795524;
const int repeats = 31;

double f(double x)
{
    double t = 2.0 + 3.0 * x;
    return (1.0 + x) / (t * t * sqrt(2.0 + x));
}

void add(double value, double& sum, double& correction)
{
    double y = value - correction;
    double t = sum + y;
    correction = (t - sum) - y;
    sum = t;
}

double partial_sum(int first, int last, double h)
{
    double sum = 0.0;
    double correction = 0.0;
    for (int i = first; i < last; i++)
    {
        double weight = (i % 2 == 0) ? 2.0 : 4.0;
        add(weight * f(a + i * h), sum, correction);
    }
    return sum;
}

double sequential(int N)
{
    double h = (b - a) / N;
    return h / 3.0 * (f(a) + f(b) + partial_sum(1, N, h));
}

struct ThreadData
{
    int first;
    int last;
    double h;
    double result;
};

void* simpson_posix(void* arg)
{
    ThreadData* data = static_cast<ThreadData*>(arg);
    data->result = partial_sum(data->first, data->last, data->h);
    return &data->result;
}

void simpson_thread(int first, int last, double h, double& result)
{
    result = partial_sum(first, last, h);
}

void* join_posix(pthread_t id)
{
    void* result = nullptr;
    if (pthread_join(id, &result) != 0)
    {
        cerr << "Ошибка ожидания завершения потока POSIX.\n";
        exit(EXIT_FAILURE);
    }
    return result;
}

struct Result
{
    double value;
    double time;
};

Result calculate(int N, int threads_count, bool use_posix)
{
    auto start = chrono::steady_clock::now();
    double h = (b - a) / N;
    int part = (N - 1) / threads_count;
    int remainder = (N - 1) % threads_count;
    vector<ThreadData> data(threads_count);
    vector<double> sums(threads_count);
    int first = 1;

    for (int i = 0; i < threads_count; i++)
    {
        int count = part + (i < remainder ? 1 : 0);
        data[i] = { first, first + count, h, 0.0 };
        first += count;
    }

    if (use_posix)
    {
        vector<pthread_t> threads(threads_count);
        for (int i = 0; i < threads_count; i++)
        {
            if (pthread_create(&threads[i], nullptr, simpson_posix, &data[i]) != 0)
            {
                for (int j = 0; j < i; j++) join_posix(threads[j]);
                throw runtime_error("Не удалось создать поток POSIX.");
            }
        }
        for (int i = 0; i < threads_count; i++)
            sums[i] = *static_cast<double*>(join_posix(threads[i]));
    }
    else
    {
        vector<thread> threads;
        threads.reserve(threads_count);
        try
        {
            for (int i = 0; i < threads_count; i++)
                threads.emplace_back(simpson_thread, data[i].first,
                    data[i].last, h, ref(sums[i]));
        }
        catch (...)
        {
            for (auto& t : threads) t.join();
            throw;
        }
        for (auto& t : threads) t.join();
    }

    double sum = f(a) + f(b);
    double correction = 0.0;
    for (double value : sums) add(value, sum, correction);
    double value = h / 3.0 * sum;
    auto end = chrono::steady_clock::now();
    double time = chrono::duration<double>(end - start).count();
    return { value, time };
}

Result measure(int N, int threads_count, bool use_posix)
{
    calculate(N, threads_count, use_posix);
    calculate(N, threads_count, use_posix);
    vector<double> times;
    double value = 0.0;
    for (int i = 0; i < repeats; i++)
    {
        Result result = calculate(N, threads_count, use_posix);
        value = result.value;
        times.push_back(result.time);
    }
    sort(times.begin(), times.end());
    return { value, times[repeats / 2] };
}

void check_accuracy()
{
    cout << "\nПроверка заданной точности\n";
    for (double eps : {0.001, 0.0001})
    {
        int N = 2;
        double previous = sequential(N);
        double value;
        double error;
        do
        {
            N *= 2;
            value = sequential(N);
            error = abs(value - previous) / 15.0;
            previous = value;
        } while (error > eps || abs(value - exact) > eps);
        cout << "\nДопустимая погрешность: " << defaultfloat << eps
            << "\nКоличество разбиений: " << N
            << "\nШаг: " << (b - a) / N
            << "\nРезультат: " << fixed << setprecision(15) << value
            << "\nАбсолютная погрешность: " << scientific << setprecision(6)
            << abs(value - exact) << '\n';
    }
}

void print_result(int threads_count, Result result, double time_one)
{
    double speedup = time_one / result.time;
    double efficiency = speedup / threads_count * 100.0;
    cout << "\nПотоки: " << threads_count
        << "\nРезультат: " << fixed << setprecision(15) << result.value
        << "\nВремя: " << setprecision(9) << result.time << " сек"
        << "\nУскорение: " << setprecision(4) << speedup
        << "\nЭффективность: " << setprecision(2) << efficiency << " %\n";
}

void test(int N, int main_threads, int max_threads, bool use_posix)
{
    cout << "\n==================================================\n"
        << (use_posix ? "POSIX Threads" : "std::thread")
        << "\nВыполняются измерения..." << flush;
    vector<Result> results;
    for (int p = 1; p <= max_threads; p++)
        results.push_back(measure(N, p, use_posix));

    cout << "\n\nОсновное вычисление\n";
    print_result(main_threads, results[main_threads - 1], results[0].time);
    cout << "Результат при одном потоке: " << setprecision(15) << results[0].value
        << "\nВремя при одном потоке: " << setprecision(9) << results[0].time << " сек\n"
        << "\nТестирование от 1 до " << max_threads << " потоков\n";
    for (int p = 1; p <= max_threads; p++)
        print_result(p, results[p - 1], results[0].time);
}

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    try
    {
        int N, main_threads, max_threads;
        cout << "Метод Симпсона. Вариант с 2.\n"
            << "Интервал интегрирования: [5; 15]\n"
            << "Введите количество разбиений: ";
        if (!(cin >> N) || N < 2 || N > 1000000000 || N % 2 != 0)
            throw runtime_error("Количество разбиений должно быть чётным числом от 2 до 1000000000.");
        cout << "Введите количество потоков для основного вычисления: ";
        if (!(cin >> main_threads) || main_threads < 1 || main_threads > 1024)
            throw runtime_error("Количество потоков должно быть от 1 до 1024.");
        cout << "Введите максимальное количество потоков для тестирования: ";
        if (!(cin >> max_threads) || max_threads < main_threads || max_threads > 1024)
            throw runtime_error("Максимум потоков должен быть не меньше основного количества и не больше 1024.");

        cout << "\nКоличество разбиений: " << N
            << "\nШаг интегрирования: " << defaultfloat << setprecision(10) << (b - a) / N
            << "\nТочное значение интеграла: " << fixed << setprecision(15) << exact
            << "\nРезультат последовательного расчёта: " << sequential(N)
            << "\nАбсолютная погрешность заданной сетки: " << scientific << setprecision(6)
            << abs(sequential(N) - exact) << '\n';
        check_accuracy();
        cout << "\nВремя каждой конфигурации: медиана " << repeats
            << " измерений после двух пробных запусков.\n";
        test(N, main_threads, max_threads, true);
        test(N, main_threads, max_threads, false);
        return 0;
    }
    catch (const exception& error)
    {
        cerr << "\nОшибка: " << error.what() << '\n';
        return 1;
    }
}
