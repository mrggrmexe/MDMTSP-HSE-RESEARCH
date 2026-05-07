# `scripts/` README

## Назначение

Папка `scripts/` содержит служебные утилиты исследовательского пайплайна MDMTSP:
генерация инстансов, запуск серий экспериментов, агрегация результатов, визуализация, валидация, экспорт отчётов, локальная сборка и CI sanity-checks.

Базовый workflow пайплайна:

```text
generate_* -> run_* -> aggregate_results.py -> plot_results.py -> export_excel_report.py
```

Результаты прогонов и производные артефакты размещаются в:
- `results/runs/`
- `results/logs/`
- `results/history/`
- `results/tables/`
- `results/plots/`
- `results/reports/`

---

## Зависимости

Python-скрипты ориентированы на Python `>= 3.12`.

Основные runtime-зависимости:
- `numpy`
- `pandas`
- `matplotlib`
- `openpyxl`

Установка:

```bash
python -m pip install -r requirements.txt
```

---

## Общие соглашения

### Формат suite-конфига

Suite-конфиги размещаются в `configs/suites/*.json`.

Минимальный пример:

```json
{
  "suite_name": "research_all",
  "algorithms": ["nearest_neighbour", "random_insertion"],
  "instance_roots": ["instances/research"],
  "seeds": [42]
}
```

### Формат run-артефакта

После `run_suite.py` в `results/runs/...` сохраняются timestamped JSON-файлы с данными одного прогона.
Скрипты агрегации и визуализации работают с любыми `*.json` под `results/runs`, а не только с именем `run.json`.

Минимально ожидаемые поля:
- `suite_name`
- `algorithm_id`
- `instance_name`
- `instance_path`
- `seed`
- `objective`
- `feasible`
- `success` или `status`
- `wall_time_ms`
- `customer_count`
- `depot_count`
- `salesman_count`
- `routes`

### Формат solution routes

Для route-visualization ожидается:

```json
"routes": [
  {"depot_id": 0, "nodes": [0, 2, 3, 0]},
  {"depot_id": 1, "nodes": [1, 4, 1]}
]
```

---

## Содержимое `scripts/`

### `add_algorithm.py`

Назначение:
- регистрация нового алгоритма в C++-кодовой базе и конфигурации запуска.

Основные действия:
- создаёт `src/mdmtsp/<name>.cpp`
- обновляет `CMakeLists.txt`
- обновляет `mdmtsp_solver.hpp`
- обновляет `main.cpp`
- добавляет алгоритм в suite-конфиги

Типовой запуск:

```bash
python scripts/add_algorithm.py cheapest_insertion
```

Перезапись шаблона алгоритма:

```bash
python scripts/add_algorithm.py cheapest_insertion --force
```

Коды завершения:
- `0` — успех
- `1` — ошибка валидации или модификации файлов

---

### `aggregate_results.py`

Назначение:
- агрегация сырых run-JSON в сводные таблицы и историю.

Вход:
- один или несколько `--search-root` с run JSON

Выход:
- `results/history/all_runs.csv`
- `results/history/all_runs.jsonl`
- `results/tables/algorithm_summary.csv`
- `results/tables/algorithm_instance_summary.csv`
- `results/tables/algorithm_instance_type_summary.csv`
- `results/tables/instance_summary.csv`

Типовой запуск:

```bash
python scripts/aggregate_results.py --search-root results/runs
```

С несколькими корнями:

```bash
python scripts/aggregate_results.py \
  --search-root results/runs \
  --search-root archived_runs
```

Параметры:
- `--search-root PATH` — каталог с run JSON, можно передавать многократно
- `--history-dir PATH` — каталог для `all_runs.*`
- `--tables-dir PATH` — каталог для агрегированных таблиц

Поведение:
- рекурсивно собирает run JSON
- нормализует статусы
- считает objective/time/gap-сводки
- пишет CSV и JSONL

---

### `check_instances.py`

Назначение:
- валидация JSON-инстансов перед массовыми прогонами.

Поддерживаемые схемы:
1. новая схема:
   - `depots: [{"id", "x", "y", "salesmen"}]`
   - `customers: [{"id", "x", "y"}]`
2. legacy-схема:
   - `depots: [{"x", "y"}]`
   - `customers: [{"x", "y"}]`
   - `salesman_count`

Что проверяет:
- корректность JSON
- наличие обязательных полей
- типы данных
- конечность координат
- уникальность `id`
- положительность числа salesmen
- базовую согласованность filename и metadata

Типовой запуск:

```bash
python scripts/check_instances.py instances/research
```

С отчётами:

```bash
python scripts/check_instances.py \
  instances/research \
  --report-csv results/reports/instance_check.csv \
  --report-jsonl results/reports/instance_check.jsonl
```

Жёсткий режим:

```bash
python scripts/check_instances.py instances/research --fail-on-warning
```

Коды завершения:
- `0` — ошибок нет
- `1` — найдены ошибки
- `2` — ошибка выполнения / некорректные аргументы

---

### `ci_sanity.sh`

Назначение:
- локальный/CI sanity-run Python- и repository-level проверок.

Что делает:
- `compileall` для `scripts/` и `tests/`
- проверку JSON-конфигов
- `bash -n` для shell-скриптов
- запуск Python unit tests
- запуск `check_instances.py`
- smoke-run `export_excel_report.py`
- smoke-run `plot_results.py`

Типовой запуск:

```bash
bash scripts/ci_sanity.sh
```

Требования:
- доступный Python в `PATH`
- установленные Python-зависимости
- заполненные `results/` и `instances/` для части проверок

Коды завершения:
- `0` — все проверки пройдены
- ненулевой — одна из стадий завершилась ошибкой

---

### `export_excel_report.py`

Назначение:
- сбор Excel workbook с исследовательской отчётностью по `results/runs` и `results/logs`.

Источники данных:
- `results/runs/**/*.json`
- `results/logs/**/*.log`

Выход:
- один `.xlsx` workbook

Основные листы workbook:
- `Overview`
- `Runs`
- `Algorithms`
- `Instances`
- `Algo_Instance`
- `Algo_Type`
- `Baseline_Matrix`
- `Failures`
- `Log_Diagnostics`
- `Parse_Issues`
- `Definitions`

Типовой запуск:

```bash
python scripts/export_excel_report.py \
  --runs-root results/runs \
  --logs-root results/logs \
  --output results/reports/research_report.xlsx
```

Поведение:
- читает timestamped run JSON
- устойчив к битым JSON и логам
- пишет parse issues в отдельный лист
- не требует предварительного `aggregate_results.py`

Параметры:
- `--runs-root PATH`
- `--logs-root PATH`
- `--output PATH`
- дополнительные параметры форматирования/фильтрации зависят от текущей реализации

Коды завершения:
- `0` — workbook успешно сформирован
- `1` — ошибка выполнения

---

### `generate_instances.py`

Назначение:
- генерация базовых или вспомогательных наборов инстансов.
- интерфейс и формат зависят от текущей реализации файла.

Рекомендуемое применение:
- только после просмотра `--help` конкретной версии скрипта.

Проверка интерфейса:

```bash
python scripts/generate_instances.py --help
```

---

### `generate_research_instances.py`

Назначение:
- генерация основной исследовательской коллекции инстансов.

Пишет:
- `instances/research/group_01/...`
- `instances/research/group_02/...`
- ...
- `instances/research/group_10/...`

Характеристики генерации:
- группы разных масштабов
- `customers` от малых до экстремально больших
- переменное число depots
- `salesmen >= depots`
- `salesmen <= customers`

Типовой запуск:

```bash
python scripts/generate_research_instances.py
```

Рекомендуется после генерации запускать:

```bash
python scripts/check_instances.py instances/research
```

---

### `generate_research_instances1.py`

Назначение:
- альтернативная или промежуточная версия генератора исследовательских инстансов.
- использовать только если текущий workflow явно на неё опирается.

Проверка интерфейса:

```bash
python scripts/generate_research_instances1.py --help
```

Практика:
- если скрипт не используется активным workflow, его следует либо документированно оставить как legacy, либо удалить.

---

### `plot_results.py`

Назначение:
- построение агрегированных графиков по таблицам `results/tables`
- массовая route-визуализация решений из `results/runs`

Вход:
- агрегированные CSV из `results/tables`
- solution JSON из `results/runs`

Основные выходы:
- `results/plots/history_comparison.png`
- `results/plots/instance_gap_heatmap.png`
- `results/plots/solutions/.../*.png`
- `results/plots/solutions/solution_render_manifest.csv`

Типовой запуск:

```bash
python scripts/plot_results.py
```

Без route-визуализации:

```bash
python scripts/plot_results.py --no-solution-visualizations
```

С лимитом на размер инстансов для solution plots:

```bash
python scripts/plot_results.py --max-solution-customers 10000
```

С перерисовкой существующих файлов:

```bash
python scripts/plot_results.py --overwrite
```

Поведение:
- route-rendering ограничивается `customer_count <= --max-solution-customers`
- абсолютные `instance_path` в run JSON пытается резолвить через `repo_root/instances`
- ошибки отдельных solution renders не должны валить агрегированные графики; детали пишутся в manifest

---

### `rebuild_macos.sh`

Назначение:
- локальная пересборка проекта на macOS.

Что умеет:
- инкрементальная сборка
- чистая пересборка (`--fresh`)
- выбор `build type`
- выбор `target`
- параллельная сборка
- опциональный запуск `ctest`

Типовой запуск:

```bash
./scripts/rebuild_macos.sh
```

Чистая пересборка + тесты:

```bash
./scripts/rebuild_macos.sh --fresh --tests
```

Debug:

```bash
./scripts/rebuild_macos.sh --build-type Debug
```

Только конкретный target:

```bash
./scripts/rebuild_macos.sh --target mdmtsp
```

Перед первым запуском:

```bash
chmod +x scripts/rebuild_macos.sh
```

---

### `run_experiments.py`

Назначение:
- legacy или вспомогательный runner для прогонов.
- в основном workflow рекомендуется `run_suite.py`.

Проверка интерфейса:

```bash
python scripts/run_experiments.py --help
```

Практика:
- если активный pipeline уже переведён на `run_suite.py`, `run_experiments.py` стоит рассматривать как compatibility layer.

---

### `run_suite.py`

Назначение:
- основной orchestrator прогонов по suite-конфигу.

Источники:
- `configs/suites/*.json`
- `instances/...`
- solver executable (`build/bin/mdmtsp`)

Выход:
- timestamped JSON в `results/runs/...`
- `.log` в `results/logs/...`

Типовой запуск:

```bash
python scripts/run_suite.py \
  --config configs/suites/research_all.json \
  --executable build/bin/mdmtsp
```

Переопределение алгоритмов:

```bash
python scripts/run_suite.py \
  --config configs/suites/research_all.json \
  --executable build/bin/mdmtsp \
  --algorithms nearest_neighbour cheapest_insertion
```

Поведение:
- перебирает `algorithms × instances × seeds`
- пишет отдельный артефакт на каждый запуск
- поддерживает suite с несколькими алгоритмами

Требования:
- собранный solver executable
- валидные suite-конфиги
- доступные instance roots

---

### `visualize_solution.py`

Назначение:
- отрисовка одного решения по solution JSON.

Вход:
- run JSON / solution JSON
- при необходимости `--instance PATH`

Выход:
- один PNG

Типовой запуск:

```bash
python scripts/visualize_solution.py \
  results/runs/.../some_run.json
```

С явным output:

```bash
python scripts/visualize_solution.py \
  results/runs/.../some_run.json \
  --output results/plots/solution_view.png
```

Если в JSON нет валидного `instance_path`:

```bash
python scripts/visualize_solution.py \
  path/to/run.json \
  --instance instances/research/group_01/example.json
```

Полезные флаги:
- `--output PATH`
- `--instance PATH`
- `--annotate-depots`
- `--route-color-mode auto|route|depot|mono`
- `--no-legend`
- `--dpi N`
- `--figure-width W`
- `--figure-height H`

Особенности реализации:
- ориентирован на корректную отрисовку до `10k` customers
- использует batched rendering, `LineCollection` и rasterization
- умеет резолвить старые абсолютные `instance_path` через repo-relative поиск в `instances/`

---

## Рекомендуемый порядок запуска

### Полный цикл

```bash
python scripts/generate_research_instances.py
python scripts/check_instances.py instances/research
./scripts/rebuild_macos.sh --fresh --tests
python scripts/run_suite.py --config configs/suites/research_until_25000.json --executable build/bin/mdmtsp
python scripts/aggregate_results.py --search-root results/runs
python scripts/plot_results.py
python scripts/export_excel_report.py --runs-root results/runs --logs-root results/logs --output results/reports/research_report.xlsx
```

### Только пересчёт аналитики и графиков

```bash
python scripts/aggregate_results.py --search-root results/runs
python scripts/plot_results.py
python scripts/export_excel_report.py --runs-root results/runs --logs-root results/logs --output results/reports/research_report.xlsx
```

### Только валидация и CI sanity

```bash
python scripts/check_instances.py instances/research
bash scripts/ci_sanity.sh
```

---

## Типовые проблемы

### `plot_results.py: no run.json files found ...`
Причина:
- пустой `results/runs`
- в `results/runs` нет JSON-артефактов прогонов

Проверка:

```bash
find results/runs -name "*.json"
```

### `file not found: /Users/.../instances/...`
Причина:
- старые run JSON содержат абсолютные пути с другой машины

Решение:
- использовать обновлённый `visualize_solution.py`, который резолвит путь через `instances/...`
- для новых прогонов сохранять repo-relative `instance_path`

### `permission denied: ./scripts/rebuild_macos.sh`
Решение:

```bash
chmod +x scripts/rebuild_macos.sh
```

### unit tests падают в CI на dynamic import
Причина:
- тест импортирует скрипт через `importlib.util`, но не добавляет модуль в `sys.modules` до `exec_module`

Правильный шаблон:

```python
SPEC = importlib.util.spec_from_file_location("module_name", SCRIPT)
module = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = module
SPEC.loader.exec_module(module)
```

---

## Минимальный набор для рабочего состояния

Для базового исследовательского цикла должны быть доступны:
- `generate_research_instances.py`
- `check_instances.py`
- `run_suite.py`
- `aggregate_results.py`
- `plot_results.py`
- `export_excel_report.py`

Для локальной разработки на macOS:
- `rebuild_macos.sh`

Для CI:
- `ci_sanity.sh`

---

## Проверка интерфейсов

Для скриптов с `argparse` рекомендуется проверять актуальный CLI через:

```bash
python scripts/<name>.py --help
```

Для shell-скриптов:

```bash
bash -n scripts/<name>.sh
```
