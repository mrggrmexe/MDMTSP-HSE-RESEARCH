# `configs/` README

## Назначение

Каталог `configs/` содержит JSON-конфиги двух классов:

1. конфиги локальных/одиночных прогонов и batch-профилей;
2. suite-конфиги для массового запуска через `scripts/run_suite.py`.

Текущий состав каталога:
- `benchmark_run.json`
- `debug.json`
- `release.json`
- `small_instances.json`
- `suites/all_instances.json`
- `suites/all_instances_random_insertion.json`
- `suites/research_all.json`
- `suites/research_group_03.json`
- `suites/research_until_25000.json`
- `suites/small_all.json`
- `suites/small_all_random_insertion.json`

---

## Структура каталога

```text
configs/
├── benchmark_run.json
├── debug.json
├── release.json
├── small_instances.json
└── suites/
    ├── all_instances.json
    ├── all_instances_random_insertion.json
    ├── research_all.json
    ├── research_group_03.json
    ├── research_until_25000.json
    ├── small_all.json
    └── small_all_random_insertion.json
```

---

## Классы конфигов

### 1. Single-run config

Используется для одного синтетического прогона или локального smoke/debug запуска.

Примеры:
- `debug.json`
- `release.json`

Схема:

```json
{
  "instance_name": "debug_run",
  "seed": 42,
  "depots": 2,
  "customers": 12,
  "salesmen": 4,
  "width": 50.0,
  "height": 50.0,
  "return_to_depot": true,
  "improve_iterations": 10,
  "output_json": false,
  "output_path": ""
}
```

Поля:
- `instance_name: string` — имя синтетического инстанса.
- `seed: integer` — seed генерации и/или стохастических процедур.
- `depots: integer` — число депо.
- `customers: integer` — число клиентов.
- `salesmen: integer` — число коммивояжёров.
- `width: number` — ширина области генерации координат.
- `height: number` — высота области генерации координат.
- `return_to_depot: boolean` — замкнутые маршруты, если `true`.
- `improve_iterations: integer` — бюджет локального улучшения.
- `output_json: boolean` — сохранять ли JSON-результат.
- `output_path: string` — путь к JSON-артефакту результата.

Ожидания:
- `depots >= 1`
- `customers >= 1`
- `salesmen >= 1`
- `salesmen >= depots` — желательно для многодепо сценариев
- `salesmen <= customers` — желательно для осмысленной загрузки
- `width > 0`
- `height > 0`
- `improve_iterations >= 0`

---

### 2. Batch profile config

Используется для набора синтетических прогонов в одном JSON.

Примеры:
- `benchmark_run.json`
- `small_instances.json`

Схема:

```json
{
  "profile": "benchmark_run",
  "description": "Профиль для более серьёзных воспроизводимых прогонов",
  "global": {
    "return_to_depot": true,
    "output_json": true
  },
  "runs": [
    {
      "instance_name": "benchmark_d2_c100_m4_s101",
      "seed": 101,
      "depots": 2,
      "customers": 100,
      "salesmen": 4,
      "width": 200.0,
      "height": 200.0,
      "improve_iterations": 100,
      "output_path": "results/benchmark_d2_c100_m4_s101.json"
    }
  ]
}
```

Поля верхнего уровня:
- `profile: string` — имя профиля.
- `description: string` — краткое техническое описание.
- `global: object` — общие значения по умолчанию для всех runs.
- `runs: array<object>` — список запусков.

Поля `global`:
- поддерживаются те же ключи, что и на уровне single-run:
  - `return_to_depot`
  - `output_json`
  - потенциально другие общие поля, если consumer их поддерживает.

Поля элементов `runs[]`:
- `instance_name`
- `seed`
- `depots`
- `customers`
- `salesmen`
- `width`
- `height`
- `return_to_depot` — опционально; может наследоваться из `global`
- `improve_iterations`
- `output_json` — опционально; может наследоваться из `global`
- `output_path`

Правило merge:
- поля из `runs[i]` имеют приоритет над `global`.

Ожидания:
- `runs` не пустой
- каждый элемент `runs` должен быть самодостаточен после merge с `global`
- `output_path` должен быть уникальным в рамках профиля

---

### 3. Suite config

Используется для массового прогона по существующим JSON-инстансам через `scripts/run_suite.py`.

Примеры:
- `suites/all_instances.json`
- `suites/research_all.json`
- `suites/research_group_03.json`
- `suites/research_until_25000.json`

Базовая схема:

```json
{
  "suite_name": "research_all",
  "description": "All generated research instances",
  "algorithms": [
    "nearest_neighbour",
    "random_insertion",
    "cheapest_insertion"
  ],
  "instance_roots": [
    "instances/research"
  ],
  "include_globs": [
    "**/*.json"
  ],
  "exclude_globs": [],
  "seeds": [
    42
  ],
  "improve_iterations": 0
}
```

Поля:
- `suite_name: string` — имя suite; используется в путях результатов.
- `description: string` — краткое техническое описание.
- `algorithms: array<string>` — список идентификаторов алгоритмов.
- `instance_roots: array<string>` — корневые каталоги для поиска входных JSON-инстансов.
- `include_globs: array<string>` — glob-маски включения.
- `exclude_globs: array<string>` — glob-маски исключения.
- `seeds: array<integer>` — список seeds для повторных прогонов.
- `improve_iterations: integer` — общий бюджет улучшения, передаваемый solver-у.

Ожидания:
- `suite_name` уникален в рамках проекта.
- `algorithms` не пустой.
- `instance_roots` не пустой.
- `seeds` не пустой.
- `improve_iterations >= 0`.

Поведение:
- `run_suite.py` строит декартово произведение  
  `algorithms × discovered_instances × seeds`.
- результаты пишутся в `results/runs/<suite>/<algorithm>/...`
- логи пишутся в `results/logs/<suite>/<algorithm>/...`

---

## Конкретные конфиги

### `debug.json`

Назначение:
- быстрый локальный прогон малого synthetic instance.

Текущее содержимое:
- `depots = 2`
- `customers = 12`
- `salesmen = 4`
- `seed = 42`
- `improve_iterations = 10`
- `output_json = false`

Использование:
- smoke-run
- ручная проверка solver-а
- быстрый прогон после локальной сборки

---

### `release.json`

Назначение:
- более крупный локальный синтетический прогон.

Текущее содержимое:
- `depots = 3`
- `customers = 100`
- `salesmen = 6`
- `seed = 2026`
- `improve_iterations = 100`
- `output_json = true`
- `output_path = results/release_run.json`

Использование:
- локальный reproducible run
- ручная оценка качества решения
- проверка записи JSON-артефакта

---

### `benchmark_run.json`

Назначение:
- batch-профиль нескольких synthetic benchmark runs.

Текущее содержимое:
- `profile = benchmark_run`
- `global.return_to_depot = true`
- `global.output_json = true`
- несколько запусков с масштабами порядка `100`, `150`, `250` клиентов

Использование:
- серия воспроизводимых локальных synthetic прогонов
- sanity-проверка поведения solver-а на нескольких размерах

---

### `small_instances.json`

Назначение:
- набор быстрых synthetic прогонов для отладки и первичной проверки корректности.

Структура:
- `profile = small_instances`
- `runs[]` содержит малые инстансы
- типично используется на раннем этапе разработки или после изменений в solver-е

Использование:
- быстрый batch smoke-test
- ручной regression run на малых synthetic cases

---

### `suites/all_instances.json`

Назначение:
- прогон по всем JSON-инстансам под `instances/`.

Текущие поля:
- `suite_name = all_instances_nn`
- `instance_roots = ["instances"]`
- `include_globs = ["**/*.json"]`
- `exclude_globs = []`
- `seeds = [42]`
- `improve_iterations = 0`
- `algorithms = ["nearest_neighbour", "cheapest_insertion"]`

Использование:
- широкий прогон по всему репозиторию
- полезен только если в `instances/` нет посторонних JSON-файлов, не являющихся инстансами

Риск:
- слишком широкий охват при наличии дополнительных JSON-файлов в `instances/`

---

### `suites/research_all.json`

Назначение:
- прогон всех research-инстансов.

Текущие поля:
- `instance_roots = ["instances/research"]`
- `include_globs = ["**/*.json"]`
- `algorithms = ["nearest_neighbour", "random_insertion", "cheapest_insertion"]`
- `seeds = [42]`

Использование:
- полный массовый прогон исследовательского набора

---

### `suites/research_group_03.json`

Назначение:
- прогон только `instances/research/group_03`.

Текущие поля:
- `instance_roots = ["instances/research/group_03"]`
- `include_globs = ["*.json"]`
- `algorithms = ["nearest_neighbour", "random_insertion", "cheapest_insertion"]`
- `seeds = [42]`

Использование:
- таргетированный прогон одной группы
- локальный profiling / tuning на среднем масштабе

---

### `suites/research_until_25000.json`

Назначение:
- прогон research-групп до 25000 клиентов.

Текущие поля:
- `instance_roots` включает `group_01` … `group_07`
- `include_globs = ["*.json"]`
- `algorithms = ["nearest_neighbour", "random_insertion", "cheapest_insertion"]`
- `seeds = [42]`

Использование:
- основной рабочий suite для экспериментов в пределах практического memory budget

Причина существования:
- алгоритмы используют distance matrix с квадратичной памятью; экстремально большие группы не подходят для стандартного режима.

---

### `suites/all_instances_random_insertion.json`

Назначение:
- suite по всем инстансам, специализированный под `random_insertion`.

Ожидаемая схема:
- совпадает с общей suite-схемой
- `algorithms` содержит только `random_insertion`

Использование:
- isolated baseline run
- выборочный пересчёт только одного алгоритма

---

### `suites/small_all.json`

Назначение:
- suite для малых инстансов по нескольким алгоритмам.

Ожидаемая схема:
- совпадает с общей suite-схемой
- `instance_roots` указывает на малые группы / тестовые подмножества

Использование:
- быстрый массовый smoke-run
- regression suite до тяжёлых прогонов

---

### `suites/small_all_random_insertion.json`

Назначение:
- малый suite только для `random_insertion`.

Использование:
- быстрый regression run одного baseline-алгоритма

---

## Схемы полей

### Поля synthetic run-конфигов

| Поле | Тип | Обязательно | Комментарий |
|---|---|---:|---|
| `instance_name` | string | да | имя synthetic instance |
| `seed` | integer | да | seed генерации |
| `depots` | integer | да | число депо |
| `customers` | integer | да | число клиентов |
| `salesmen` | integer | да | число коммивояжёров |
| `width` | number | да | ширина области |
| `height` | number | да | высота области |
| `return_to_depot` | boolean | обычно да | замкнутый маршрут |
| `improve_iterations` | integer | да | бюджет локального улучшения |
| `output_json` | boolean | обычно да | писать JSON-результат |
| `output_path` | string | если `output_json=true` | путь результата |

### Поля suite-конфигов

| Поле | Тип | Обязательно | Комментарий |
|---|---|---:|---|
| `suite_name` | string | да | имя suite |
| `description` | string | нет | описание |
| `algorithms` | array<string> | да | список алгоритмов |
| `instance_roots` | array<string> | да | каталоги поиска |
| `include_globs` | array<string> | да | include-маски |
| `exclude_globs` | array<string> | да | exclude-маски |
| `seeds` | array<int> | да | список seeds |
| `improve_iterations` | integer | да | общий бюджет улучшения |

---

## Идентификаторы алгоритмов

В suite-конфигах и связанных инструментах используются строковые `algorithm_id`.

Подтверждённые идентификаторы в текущих конфигах:
- `nearest_neighbour`
- `random_insertion`
- `cheapest_insertion`

При добавлении нового алгоритма через `scripts/add_algorithm.py` его `algorithm_id` должен совпадать:
- с названием, используемым в `main.cpp`
- с названием функции/обработчика solver-а
- с названием в suite-конфигах

---

## Пути и glob-маски

### `instance_roots`

Все пути в `instance_roots` задаются относительно корня репозитория.

Корректные примеры:
- `instances`
- `instances/research`
- `instances/research/group_03`

### `include_globs`

Примеры:
- `*.json` — только файлы текущего каталога
- `**/*.json` — рекурсивно все JSON-файлы

### `exclude_globs`

Используются для отсеивания нежелательных подмножеств.
Примеры:
- `**/deprecated/*.json`
- `**/*_broken.json`

---

## Валидация конфигов

Минимальная техническая проверка:
- JSON должен парситься
- обязательные поля должны существовать
- числа должны быть конечными и иметь допустимые диапазоны
- массивы `algorithms`, `instance_roots`, `seeds` не должны быть пустыми

Проверка JSON-синтаксиса:

```bash
python - <<'PY'
import json
from pathlib import Path
for path in Path("configs").rglob("*.json"):
    json.loads(path.read_text(encoding="utf-8"))
    print("ok", path)
PY
```

В CI это уже частично покрывается `scripts/ci_sanity.sh`, который валидирует JSON-конфиги.

---

## Типовые операции

### Просмотреть доступные suite-конфиги

```bash
find configs/suites -name "*.json" | sort
```

### Запустить основной research suite

```bash
python scripts/run_suite.py   --config configs/suites/research_until_25000.json   --executable build/bin/mdmtsp
```

### Запустить полный research suite

```bash
python scripts/run_suite.py   --config configs/suites/research_all.json   --executable build/bin/mdmtsp
```

### Переопределить алгоритмы поверх suite-конфига

```bash
python scripts/run_suite.py   --config configs/suites/research_group_03.json   --executable build/bin/mdmtsp   --algorithms nearest_neighbour
```

### Проверить JSON-конфиги перед commit

```bash
python -m json.tool configs/debug.json > /dev/null
python -m json.tool configs/release.json > /dev/null
find configs -name "*.json" -print0 | while IFS= read -r -d '' f; do
  python -m json.tool "$f" > /dev/null || exit 1
done
```

---

## Рекомендации по изменению конфигов

### При добавлении нового suite

Обязательно задать:
- `suite_name`
- `algorithms`
- `instance_roots`
- `include_globs`
- `exclude_globs`
- `seeds`
- `improve_iterations`

### При добавлении нового алгоритма

Нужно синхронно обновить:
- C++-регистрацию алгоритма
- suite-конфиги
- при необходимости отдельные algorithm-specific suite-файлы

### При изменении `instance_roots`

Проверять:
- реально ли существуют каталоги
- не захватывают ли glob-маски лишние JSON-файлы
- не включают ли неподдерживаемые инстансы

### При изменении `seeds`

Следить за:
- воспроизводимостью
- кратностью числа прогонов
- корректностью downstream-агрегации

---

## Типовые ошибки

### Пустой прогон suite

Причины:
- неверный `instance_roots`
- слишком узкий `include_globs`
- слишком широкий `exclude_globs`

Проверка:
```bash
python - <<'PY'
from pathlib import Path
root = Path("instances/research")
print(sum(1 for _ in root.rglob("*.json")))
PY
```

### Слишком тяжёлый suite

Причина:
- включены группы с слишком большим числом клиентов для distance-matrix подхода.

Решение:
- использовать `research_until_25000.json`
- создать отдельный bounded suite

### Конфликт `output_path` в batch profile

Причина:
- два `runs[]` пишут в один и тот же файл.

Решение:
- сделать уникальные `output_path` для каждого synthetic run.

### Неподдерживаемый `algorithm_id`

Причина:
- имя алгоритма есть в JSON, но отсутствует в solver registration.

Решение:
- синхронизировать конфиги и C++-код.

---

## Минимальный рабочий набор конфигов

Для повседневной работы достаточно:
- `configs/debug.json`
- `configs/release.json`
- `configs/suites/research_until_25000.json`
- `configs/suites/research_group_03.json`

Для полного исследования:
- все `configs/suites/research_*.json`
- algorithm-specific suites при необходимости
- batch synthetic profiles (`benchmark_run.json`, `small_instances.json`) для вспомогательных локальных прогонов
