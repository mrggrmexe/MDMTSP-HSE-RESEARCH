# `tests/` README

## Назначение

Каталог `tests/` содержит автоматические проверки вычислительного ядра и вспомогательного tooling.

`tests/` используется для:
- проверки корректности data model;
- проверки корректности objective и validation logic;
- проверки корректности solver-компонентов на малых эталонных кейсах;
- проверки Python-утилит, влияющих на reproducibility pipeline;
- защиты от регрессий при изменении `src/`, `scripts/`, `configs/`, `instances/`.

Тестовый слой должен оставаться независимым от конкретных экспериментальных результатов в `results/` и от локальных путей разработчика.

---

## Архитектурная роль

`tests/` является верхним уровнем контроля качества репозитория.

Каталог должен покрывать четыре класса проверок:

1. **C++ unit tests**
   - проверка локальных модулей из `src/`;
   - проверка инвариантов структур данных;
   - проверка objective / validator / operator logic.

2. **C++ small-case integration tests**
   - проверка полного solver pipeline на малых инстансах;
   - проверка базовой согласованности assignment + construction + improvement + validation.

3. **Python unit / script tests**
   - проверка служебных скриптов из `scripts/`;
   - проверка генерации артефактов (`xlsx`, `png`, `csv`, manifests);
   - проверка path resolution, JSON parsing, CLI behaviour.

4. **Repository-level sanity checks**
   - выполняются через `scripts/ci_sanity.sh`;
   - включают синтаксические, конфигурационные и smoke-level проверки.

---

## Рекомендуемая структура

В простом варианте `tests/` может быть плоским каталогом.

Рекомендуемая логическая структура:

```text
tests/
├── cpp/
│   ├── unit/
│   ├── integration/
│   └── fixtures/
├── python/
│   ├── unit/
│   ├── cli/
│   └── fixtures/
├── data/
│   ├── instances/
│   ├── runs/
│   └── logs/
└── <legacy flat files>
```

Если каталог остаётся плоским, необходимо сохранять смысловое разделение через naming conventions.

Допустимый текущий плоский стиль:
- `test_*.cpp`
- `test_*.py`

---

## Принципы проектирования тестов

### 1. Детерминизм

Любой тест должен:
- либо быть полностью детерминированным;
- либо фиксировать `seed` явно.

Нельзя полагаться на:
- системное время;
- случайные данные без фиксированного источника;
- состояние внешней среды.

### 2. Малый масштаб

Тесты не должны использовать крупные production-sized инстансы.

Цель:
- быстрый прогон;
- воспроизводимость;
- локализация ошибки.

Для unit/integration test-слоя предпочтительны:
- минимальные synthetic instances;
- hand-crafted edge cases;
- небольшие временные временные каталоги / temp-files.

### 3. Независимость

Тест не должен зависеть от:
- результатов другого теста;
- порядка исполнения;
- already existing `results/` артефактов;
- machine-specific absolute paths.

### 4. Локализуемость ошибки

Один тест должен проверять одну логическую гипотезу или один тесно связанный инвариант.

Нежелательно:
- объединять множество независимых утверждений в один test case;
- строить “сценарий на всё сразу”, если ошибка потом трудно локализуется.

### 5. Минимум неявной логики

Тест должен быть читаем как формулировка контракта:
- вход;
- действие;
- ожидаемый результат.

---

## Классы тестов

## C++ unit tests

Проверяют отдельные модули из `src/`.

Типовые цели:
- `instance` / `instance_io`
- `objective`
- `validator`
- `solution`
- `assignment`
- route-level operators
- inter-route operators
- utility functions

Требования:
- не использовать heavy benchmarks;
- не читать production-sized JSON без необходимости;
- явно проверять численные результаты и инварианты.

Типовые примеры проверок:
- значение objective на hand-crafted route;
- корректность validation при duplicate customer;
- корректность open/closed route semantics;
- корректность delta computation;
- корректность parsing instance JSON.

---

## C++ integration tests

Проверяют взаимодействие нескольких модулей.

Типовые цели:
- построение полного решения на малом инстансе;
- совместимость construction + improvement;
- корректность экспортируемого run result;
- корректность small-case solver behaviour.

Особенности:
- допускают чуть больший объём логики, чем unit tests;
- должны оставаться быстрыми;
- не должны требовать Python pipeline.

---

## Python unit tests

Проверяют внутреннее поведение Python-скриптов.

Типовые цели:
- parsing/validation;
- workbook generation;
- aggregation logic;
- route rendering helpers;
- path resolution;
- collect/build/write функции.

Особенности:
- для тестирования модуля скрипт можно импортировать как модуль;
- при использовании `importlib.util` модуль должен быть добавлен в `sys.modules` до `exec_module(...)`.

Корректный шаблон динамического импорта:

```python
SPEC = importlib.util.spec_from_file_location("module_name", SCRIPT)
module = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = module
SPEC.loader.exec_module(module)
```

---

## Python CLI tests

Проверяют скрипт как внешний исполняемый интерфейс.

Типовые цели:
- корректный exit code;
- создание ожидаемого файла;
- корректный stderr/stdout;
- устойчивость к битым JSON и отсутствующим путям.

Инструмент:
- `subprocess.run(...)`

CLI tests предпочтительнее unit-style вызова функции `main(...)`, если:
- `main()` не принимает `argv`;
- скрипт сильно завязан на `argparse` и `sys.argv`;
- нужно проверить полный behaviour внешней команды.

---

## Repository-level sanity tests

Исполняются через:

```bash
bash scripts/ci_sanity.sh
```

Покрывают:
- `compileall` Python sources;
- JSON config validation;
- shell syntax checks;
- Python unit tests;
- smoke-run ключевых служебных скриптов.

Это не замена unit tests, а дополнительный слой.

---

## Соглашения по именованию

### Общие правила

Файлы тестов должны именоваться так, чтобы runner мог автоматически их обнаружить.

Рекомендуемые шаблоны:
- `test_<subject>.cpp`
- `test_<subject>.py`

Примеры subject-ов:
- `instance`
- `instance_io`
- `objective`
- `validator`
- `tsp_operators`
- `mdmtsp_small_cases`
- `check_instances`
- `export_excel_report`
- `visualize_solution`

### Именование test case-ов

Имя теста должно описывать проверяемый контракт:
- `test_valid_new_schema`
- `test_duplicate_id_is_error`
- `test_build_export_bundle`
- `test_reports_missing_instance_path`

Нежелательные имена:
- `test1`
- `test_ok`
- `test_misc`

---

## Fixture strategy

### Inline fixtures

Подходят для:
- очень малых JSON payload;
- unit tests с 1–5 узлами;
- простых edge cases.

Преимущества:
- высокая локальность;
- минимум внешних файлов;
- проще читать контракт теста.

### File-based fixtures

Подходят для:
- повторно используемых инстансов;
- многократных integration tests;
- более сложных структурированных входов.

Рекомендуемое размещение:

```text
tests/data/
├── instances/
├── runs/
├── logs/
└── configs/
```

### Temporary directories

Для файловых Python tests рекомендуется:
- создавать `TemporaryDirectory()`;
- генерировать файлы теста внутри temp-root;
- не писать в реальные `results/` или `instances/` каталоги репозитория.

Это исключает побочные эффекты и зависимость от локального состояния.

---

## Что должно тестироваться обязательно

## Для `src/`

Минимальный обязательный набор:
- корректность чтения/представления instance;
- корректность objective;
- корректность validator;
- корректность хотя бы одного полного small-case solve path;
- корректность route/operator semantics.

## Для `scripts/`

Минимальный обязательный набор:
- `check_instances.py`
- `export_excel_report.py`
- `visualize_solution.py`
- при наличии стабильного API — `plot_results.py`, `aggregate_results.py`

## Для CI/репозитория

Минимальный обязательный набор:
- JSON-конфиги валидны;
- shell scripts синтаксически корректны;
- Python-код компилируется;
- unit tests проходят.

---

## Что не должно тестироваться в `tests/`

Нежелательно:
- production-sized experiments;
- массовые прогонные серии на десятках тысяч клиентов;
- performance benchmarking как часть обычного CI;
- тесты, требующие ручного ввода;
- тесты, использующие локальные абсолютные пути;
- тесты, опирающиеся на нестабильные артефакты в `results/`.

Для больших performance runs следует использовать:
- `configs/suites/*.json`
- `scripts/run_suite.py`
- отдельные experiment workflows

---

## Запуск тестов

## C++ tests через CTest

После сборки:

```bash
ctest --test-dir build --output-on-failure
```

Или из build-каталога:

```bash
cd build
ctest --output-on-failure
```

### Локальный rebuild + tests на macOS

```bash
./scripts/rebuild_macos.sh --fresh --tests
```

---

## Python tests через unittest

Запуск всех Python tests:

```bash
python -m unittest discover -s tests -p "test_*.py"
```

Запуск одного файла:

```bash
python -m unittest tests.test_check_instances
```

или:

```bash
python tests/test_check_instances.py
```

---

## Полный sanity run

```bash
bash scripts/ci_sanity.sh
```

---

## Ожидаемое поведение test runners

### CTest

Каждый C++ test должен:
- возвращать `0` при успехе;
- возвращать ненулевой код при провале;
- печатать диагностически полезное сообщение об ошибке.

### Python unittest

Каждый test file должен:
- быть импортируемым без побочных эффектов, кроме подготовки тестового модуля;
- использовать стандартный `unittest` entrypoint.

---

## Требования к новым тестам

При добавлении нового теста нужно определить:

1. что именно он проверяет;
2. к какому слою он относится:
   - C++ unit
   - C++ integration
   - Python unit
   - Python CLI
   - repository sanity
3. нужен ли ему file fixture;
4. нужен ли ему temp directory;
5. должен ли он работать без build artifacts;
6. должен ли он исполняться в обычном CI.

### Для нового C++ модуля

Минимум:
- хотя бы один unit test на основной контракт;
- edge case на некорректный вход или граничную ситуацию.

### Для нового Python-скрипта

Минимум:
- один test на успешный сценарий;
- один test на failure/diagnostic path;
- если скрипт создаёт артефакт, проверить факт создания и базовую структуру.

---

## Границы между C++ и Python tests

### Что проверяется C++ tests
- математическая и алгоритмическая корректность ядра;
- корректность data structures;
- correctness of solver logic;
- small-case route/solution behaviour.

### Что проверяется Python tests
- корректность служебного tooling;
- корректность transform/export/render слоёв;
- корректность CLI и artifact generation;
- корректность path handling и file IO.

Python tests не должны дублировать алгоритмическое ядро C++.

---

## Требования к артефактам, создаваемым в тестах

Если тест создаёт:
- `png`
- `xlsx`
- `csv`
- `json`
- `log`

то он должен:
- создавать их во временном каталоге;
- очищать ресурсы автоматически через `TemporaryDirectory`;
- не полагаться на существование файлов между тестами.

---

## Типовые ошибки и анти-паттерны

### 1. Absolute paths

Нельзя использовать:
- `/Users/...`
- `/home/<user>/...`
- `/mnt/data/...` как жёстко зашитый production-путь в репозиторных тестах.

Нужно использовать:
- `Path(__file__).resolve().parents[...]`
- `TemporaryDirectory()`
- repo-relative path resolution

### 2. Импорт скрипта без регистрации в `sys.modules`

Проблема:
- `dataclass`/runtime introspection может ломаться при dynamic import.

Решение:
- регистрировать модуль в `sys.modules` до `exec_module`.

### 3. Тесты на слишком крупные данные

Проблема:
- медленный CI;
- нестабильность;
- трудная локализация ошибки.

Решение:
- использовать минимальные synthetic cases.

### 4. Проверка слишком большого числа контрактов в одном тесте

Проблема:
- при падении трудно понять причину.

Решение:
- дробить тесты по одной гипотезе.

### 5. Тестирование внутренней реализации вместо контракта

Проблема:
- тесты становятся хрупкими при рефакторинге.

Решение:
- проверять observable behaviour, а не частные внутренние детали.

---

## Рекомендуемые уровни покрытия

`tests/` не обязан покрывать каждую строку кода, но должен покрывать:
- все ключевые data contracts;
- все критические objective/validation paths;
- все публичные/пользовательские CLI-скрипты;
- все известные ранее баги, если они были исправлены.

Полезная практика:
- после фикса бага добавлять regression test, который падает без фикса и проходит после него.

---

## Инварианты тестового слоя

### Инвариант 1

Тесты должны быть воспроизводимы на локальной машине и в CI.

### Инвариант 2

Тесты не должны зависеть от конкретного содержимого рабочего `results/` каталога пользователя.

### Инвариант 3

Тесты должны оставаться валидными при расширении `src/`, если публичный контракт не менялся.

### Инвариант 4

Провал теста должен давать диагностически полезную причину.

### Инвариант 5

Тестовый слой должен быть быстрее, чем экспериментальный слой на порядки.

---

## Минимальный рабочий набор test-layer

Для рабочего состояния репозитория должны существовать:
- C++ tests, запускаемые через `ctest`;
- Python tests, запускаемые через `unittest`;
- `scripts/ci_sanity.sh` как repository-level gate.

Минимальный набор сценариев:
- objective correctness;
- validator correctness;
- small-case solver correctness;
- instance-checking;
- workbook export;
- solution rendering.

---

## Рекомендуемый поток при разработке

### После изменения `src/`

```bash
./scripts/rebuild_macos.sh --tests
```

или:

```bash
ctest --test-dir build --output-on-failure
```

### После изменения `scripts/`

```bash
python -m unittest discover -s tests -p "test_*.py"
bash scripts/ci_sanity.sh
```

### Перед push

```bash
ctest --test-dir build --output-on-failure
python -m unittest discover -s tests -p "test_*.py"
bash scripts/ci_sanity.sh
```

---

## Связь с CI

CI должен использовать `tests/` как основной источник автоматической проверки.

Ожидаемое распределение:
- C++ build/test stage -> `ctest`
- Python/repository sanity stage -> `unittest` + `ci_sanity.sh`

Если тест не должен выполняться в обычном CI, это должно быть явно отражено:
- отдельным именованием;
- отдельным каталогом;
- отдельным workflow;
- или явным skip/marking policy.

---

## Требования к эволюции `tests/`

Так как `src/`, `scripts/`, `configs/` и `instances/` будут развиваться, test-layer должен поддерживать:

1. расширение без переписывания базового harness-а;
2. отделение stable contracts от временных implementation details;
3. обратную совместимость тестов с реальными CLI/data contracts;
4. быстрый локальный запуск на рабочей машине;
5. минимальную хрупкость при рефакторинге.

Каждый новый тест должен отвечать на вопрос:
- какой публичный контракт репозитория он защищает.

Если ответ неочевиден, тест либо лишний, либо находится не на том уровне абстракции.
