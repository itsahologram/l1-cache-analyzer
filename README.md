# Cache Analyzer

Программа для экспериментального определения характеристик кэша данных L1:

* размер cache line;
* объём L1 cache;
* ассоциативность.

## Сборка

```bash
cmake -B build
cmake --build build
```

## Запуск

```bash
./build/cache_analyzer
```

Пример вывода:

```text
Cache line: 64 B
Cache capacity ~= 32 KB
Associativity ~= 8-way
```

## Debug

Для сборки с отладочным выводом:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/cache_analyzer
```

В Debug-режиме программа дополнительно выводит результаты промежуточных измерений.
