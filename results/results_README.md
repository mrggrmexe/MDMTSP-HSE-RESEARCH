# `results/` README

## Назначение

Каталог `results/` содержит все артефакты вычислительных экспериментов:
- сырые результаты отдельных прогонов;
- логи запусков;
- агрегированную историю;
- сводные таблицы;
- графики;
- Excel-отчёты и производные артефакты.

`results/` является downstream-слоем для:
- `scripts/run_suite.py`
- `scripts/aggregate_results.py`
- `scripts/plot_results.py`
- `scripts/export_excel_report.py`

Базовая зависимость слоёв:

```text
results/runs + results/logs
    -> results/history + results/tables
    -> results/plots + results/reports
```

---

## Структура каталога

Ожидаемая структура:

```text
results/
├── history/
├── logs/
├── plots/
├── reports/
├── runs/
├── run_summaries/
└── tables/
```

Текущие подтверждённые подкаталоги в рабочем пайплайне:
- `results/runs/`
- `results/logs/`
- `results/history/`
- `results/tables/`
- `results/plots/`

`results/reports/` и `results/run_summaries/` могут создаваться по мере использования отдельных скриптов.

---

## Семантика подкаталогов

### `results/runs/`

Содержит сырые структурированные артефакты отдельных прогонов.

Источник:
- `scripts/run_suite.py`

Формат размещения:
- `results/runs/<suite>/<algorithm>/.../*.json`

В текущем пайплайне используются timestamped JSON-файлы, а не только имя `run.json`.

Типовой путь:

```text
results/runs/research_until_25000/random_insertion/research/group_07/g07_i02_c25000_d5_m26_random/20260506T005901Z__research_until_25000__random_insertion__g07_i02_c25000_d5_m26_random__seed_42__72ac6c8b5fdd.json
```

Содержимое одного run-JSON должно описывать один запуск алгоритма на одном инстансе и одном seed.

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

Дополнительные поля допускаются.

Назначение:
- source-of-truth для агрегации;
- источник route-visualization;
- источник Excel export;
- база для последующего статистического анализа.

---

### `results/logs/`

Содержит текстовые логи отдельных прогонов.

Источник:
- `scripts/run_suite.py`

Формат размещения:
- `results/logs/<suite>/<algorithm>/.../*.log`

Типовой путь:

```text
results/logs/research_until_25000/cheapest_insertion/research/group_01/g01_i01_c10_d1_m3_random/20260506T005954Z__research_until_25000__cheapest_insertion__g01_i01_c10_d1_m3_random__seed_42__7e06f097d45e.log
```

Назначение:
- диагностика падений и некорректных прогонов;
- fallback-источник для Excel diagnostics;
- трассировка run-level поведения solver-а.

Лог не является заменой run-JSON:
- агрегаторы и визуализаторы ориентированы прежде всего на JSON-артефакты.

---

### `results/history/`

Содержит плоскую историю всех собранных прогонов.

Источник:
- `scripts/aggregate_results.py`

Подтверждённые файлы:
- `all_runs.csv`
- `all_runs.jsonl`

Назначение:
- единая нормализованная история run-level артефактов;
- вход для анализа в pandas / notebooks / downstream tooling;
- промежуточный слой между сырыми данными и агрегированными таблицами.

#### `all_runs.csv`

Строка = один прогон.

Ожидаемые поля:
- suite / algorithm / instance / seed
- objective / feasible / success
- time metrics
- metadata инстанса
- derived metrics (в зависимости от версии агрегатора)

#### `all_runs.jsonl`

JSONL-представление той же истории.
Удобно для:
- потоковой обработки;
- CLI-фильтрации;
- архивирования промежуточного слоя.

---

### `results/tables/`

Содержит агрегированные CSV-сводки.

Источник:
- `scripts/aggregate_results.py`

Подтверждённые файлы:
- `algorithm_summary.csv`
- `algorithm_instance_summary.csv`
- `algorithm_instance_type_summary.csv`
- `instance_summary.csv`

Назначение:
- основа для aggregate plots;
- основа для Excel report;
- удобный табличный слой для отчёта и paper tables.

#### `algorithm_summary.csv`

Уровень агрегации:
- один алгоритм = одна строка.

Типовые поля:
- `algorithm_id`
- `runs`
- `successful_runs`
- `feasible_runs`
- `feasible_rate`
- `objective_comparable_runs`
- `time_comparable_runs`
- `unique_instances`
- `unique_instance_types`
- `instance_types`
- `unique_suites`
- `suite_names`
- `best_run_count`
- `best_instance_coverage`
- `fastest_run_count`
- `fastest_instance_coverage`
- `min_gap_to_best_observed`
- `mean_gap_to_best_observed`
- `median_gap_to_best_observed`
- `min_time_ratio_to_fastest`
- `mean_time_ratio_to_fastest`
- `median_time_ratio_to_fastest`
- `min_time_gap_to_fastest`
- `mean_time_gap_to_fastest`
- `median_time_gap_to_fastest`
- `total_wall_time_s`
- `mean_wall_time_ms`
- `median_wall_time_ms`
- `p90_wall_time_ms`
- `median_wall_time_per_customer_us`

#### `algorithm_instance_summary.csv`

Уровень агрегации:
- алгоритм × инстанс.

Типовые поля:
- `algorithm_id`
- `instance_name`
- `instance_type`
- `runs`
- `successful_runs`
- `feasible_runs`
- `feasible_rate`
- `suite_names`
- `depot_count`
- `customer_count`
- `salesman_count`
- `return_to_depot`
- `best_objective`
- `mean_objective`
- `median_objective`
- `min_gap_to_best_observed`
- `mean_gap_to_best_observed`
- `median_gap_to_best_observed`
- `min_time_ratio_to_fastest`
- `mean_time_ratio_to_fastest`
- `median_time_ratio_to_fastest`
- `min_time_gap_to_fastest`
- `mean_time_gap_to_fastest`
- `median_time_gap_to_fastest`
- `total_wall_time_s`
- `mean_wall_time_ms`
- `median_wall_time_ms`
- `p90_wall_time_ms`
- `median_wall_time_per_customer_us`

#### `algorithm_instance_type_summary.csv`

Уровень агрегации:
- алгоритм × тип инстанса.

Типовые поля:
- `algorithm_id`
- `instance_type`
- `runs`
- `successful_runs`
- `feasible_runs`
- `unique_instances`
- `median_gap_to_best_observed`
- `mean_gap_to_best_observed`
- `median_time_gap_to_fastest`
- `mean_time_gap_to_fastest`
- `median_wall_time_ms`
- `p90_wall_time_ms`
- `median_wall_time_per_customer_us`

Подтверждённые instance types:
- `random`
- `clustered`
- `grid`
- `line`
- `adversarial`

#### `instance_summary.csv`

Уровень агрегации:
- один инстанс = одна строка.

Типовые поля:
- `instance_name`
- `instance_type`
- `runs`
- `successful_runs`
- `feasible_runs`
- `feasible_rate`
- `algorithms_tested`
- `seeds_tested`
- `best_observed_objective`
- `best_algorithm_ids`
- `fastest_observed_wall_time_ms`
- `fastest_algorithm_ids`
- `depot_count`
- `customer_count`
- `salesman_count`
- `return_to_depot`
- `median_wall_time_ms`
- `p90_wall_time_ms`

---

### `results/plots/`

Содержит графические артефакты.

Источник:
- `scripts/plot_results.py`
- косвенно `scripts/visualize_solution.py`

Подтверждённые aggregate plots:
- `history_comparison.png`
- `instance_gap_heatmap.png`

Дополнительные route-level артефакты:
- `results/plots/solutions/.../*.png`
- `results/plots/solutions/solution_render_manifest.csv`

#### `history_comparison.png`

Назначение:
- сравнение алгоритмов по качеству/времени на агрегированном уровне.

Источник данных:
- CSV из `results/tables/`

#### `instance_gap_heatmap.png`

Назначение:
- визуализация относительного качества алгоритмов по инстансам или группам инстансов.

Источник данных:
- `algorithm_instance_summary.csv`
- дополнительные derived tables внутри `plot_results.py`

#### `plots/solutions/`

Назначение:
- отрисовка маршрутов отдельных решений.

Источник:
- `results/runs/**/*.json`
- `instances/...`

Типовой путь:

```text
results/plots/solutions/<suite>/<algorithm>/<group>/<instance>/<run_json_name>.png
```

Особенности:
- route-visualization обычно ограничивается инстансами с `customer_count <= threshold`;
- ошибки route-rendering пишутся в manifest и не должны уничтожать aggregate plots.

#### `solution_render_manifest.csv`

Назначение:
- журнал массовой route-отрисовки.

Типовые поля:
- путь к solution JSON
- статус (`rendered`, `skipped`, `error`)
- путь к output PNG
- сообщение об ошибке / причина пропуска
- metadata по инстансу и алгоритму

---

### `results/reports/`

Содержит экспортированные отчёты высокого уровня.

Источник:
- `scripts/export_excel_report.py`

Типовой файл:
- `research_report.xlsx`

Назначение:
- единый Excel workbook для исследовательской отчётности;
- удобный просмотр результатов вне Python;
- подготовка материалов для отчёта/защиты.

Ожидаемые листы workbook:
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

`results/reports/` может отсутствовать, если Excel export ещё не запускался.

---

### `results/run_summaries/`

Назначение:
- зарезервированный слой для дополнительных per-run summary артефактов.

Может использоваться для:
- сокращённых summary JSON/CSV;
- derived statistics на уровне одного запуска;
- future tooling.

Если каталог не используется текущими скриптами, его можно считать optional.

---

## Источники данных и производные артефакты

### Уровень 1: сырые данные

`results/runs/`
- структурированные run JSON

`results/logs/`
- текстовые логи

### Уровень 2: нормализованная история

`results/history/`
- плоская история прогонов

### Уровень 3: агрегированные таблицы

`results/tables/`
- summary CSV на нескольких уровнях агрегации

### Уровень 4: представление

`results/plots/`
- PNG/CSV для визуализации

`results/reports/`
- Excel workbook

---

## Формат run-артефактов

### Обязательные свойства run JSON

Для корректной работы пайплайна рекомендуется, чтобы run JSON содержал:

| Поле | Тип | Назначение |
|---|---|---|
| `suite_name` | string | имя suite |
| `algorithm_id` | string | идентификатор алгоритма |
| `instance_name` | string | имя инстанса |
| `instance_path` | string | путь к инстансу |
| `seed` | integer | seed прогона |
| `objective` | number | целевая функция |
| `feasible` | boolean | допустимость решения |
| `success` / `status` | boolean/string | успешность запуска |
| `wall_time_ms` | number | wall-clock время |
| `customer_count` | integer | число клиентов |
| `depot_count` | integer | число депо |
| `salesman_count` | integer | число коммивояжёров |
| `routes` | array | маршруты решения |

### `instance_path`

Рекомендуемый формат:
- repo-relative путь, например:
  `instances/research/group_03/g03_i01_c500_d5_m21_random.json`

Нежелательный формат:
- абсолютные пути локальной машины:
  `/Users/.../instances/...`

Причина:
- абсолютные пути непереносимы между машинами и ломают CI/route-visualization.

---

## Генерация артефактов

### Сырые run/log артефакты

```bash
python scripts/run_suite.py \
  --config configs/suites/research_until_25000.json \
  --executable build/bin/mdmtsp
```

### Агрегация истории и таблиц

```bash
python scripts/aggregate_results.py --search-root results/runs
```

### Построение графиков

```bash
python scripts/plot_results.py
```

### Экспорт Excel report

```bash
python scripts/export_excel_report.py \
  --runs-root results/runs \
  --logs-root results/logs \
  --output results/reports/research_report.xlsx
```

---

## Типовые команды диагностики

### Найти все run JSON

```bash
find results/runs -name "*.json"
```

### Найти все логи

```bash
find results/logs -name "*.log"
```

### Проверить наличие aggregate tables

```bash
find results/tables -maxdepth 1 -name "*.csv" | sort
```

### Проверить наличие графиков

```bash
find results/plots -name "*.png" | sort
```

### Найти битые/подозрительные run JSON

```bash
find results/runs -name "*.json" -print0 | while IFS= read -r -d '' f; do
  python -m json.tool "$f" > /dev/null || echo "bad: $f"
done
```

### Проверить, есть ли routes в run JSON

```bash
python - <<'PY'
import json
from pathlib import Path
for path in Path("results/runs").rglob("*.json"):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        continue
    if "routes" not in data:
        print(path)
PY
```

---

## Типовые проблемы

### `plot_results.py: no run.json files found ...`

Причина:
- старый consumer ожидает имя `run.json`, но фактические артефакты timestamped.

Решение:
- consumer должен искать все `*.json` под `results/runs`;
- текущая версия `plot_results.py` должна поддерживать timestamped JSON.

### Route-visualization не находит instance file

Причина:
- в `instance_path` сохранён абсолютный путь локальной машины.

Решение:
- сохранять repo-relative `instance_path`;
- использовать `visualize_solution.py`, который умеет резолвить путь через `repo_root/instances`.

### `results/plots/solutions` содержит ошибки

Причины:
- неподдерживаемый формат `routes`;
- отсутствует `instance_path`;
- отсутствует файл инстанса;
- слишком большой инстанс и включён жёсткий режим рендера.

Решение:
- смотреть `solution_render_manifest.csv`.

### `results/tables` пустой

Причина:
- не запускался `aggregate_results.py`;
- в `results/runs` нет валидных JSON.

### `results/reports/research_report.xlsx` не создаётся

Причина:
- не установлен `openpyxl`;
- битые JSON/логи;
- ошибка в Excel-export слое.

Проверка:
```bash
python scripts/export_excel_report.py --runs-root results/runs --logs-root results/logs --output results/reports/research_report.xlsx
```

---

## Политика хранения

### Что коммитить

Обычно допустимо коммитить:
- агрегированные таблицы (`results/tables`)
- selected plots (`results/plots`)
- небольшие history snapshots (`results/history`)
- report workbook при необходимости

### Что не коммитить без необходимости

Нежелательно коммитить в большом объёме:
- все сырые `results/runs/**/*.json`
- все `results/logs/**/*.log`
- массовые route-visualizations на тысячи PNG

Причины:
- быстрый рост объёма репозитория;
- дублирование производных артефактов;
- ухудшение CI / clone time.

### Практика

Рекомендуемый подход:
- сырой большой результат хранить вне git или в отдельных archive-ветках;
- в основном репозитории держать компактные summary-артефакты.

---

## Очистка и пересчёт

Если требуется полная пересборка аналитики:

```bash
rm -rf results/history results/tables results/plots results/reports
python scripts/aggregate_results.py --search-root results/runs
python scripts/plot_results.py
python scripts/export_excel_report.py --runs-root results/runs --logs-root results/logs --output results/reports/research_report.xlsx
```

Если нужно сохранить runs/logs и пересчитать только производные слои:
- удалять только `history/tables/plots/reports`;
- `runs/` и `logs/` оставлять.

---

## Минимальный рабочий набор `results/`

Для базовой работы пайплайна должны существовать:
- `results/runs/`
- `results/logs/`

Для анализа:
- `results/history/`
- `results/tables/`

Для визуализации:
- `results/plots/`

Для отчётности:
- `results/reports/`

---

## Инварианты каталога

### Инвариант 1

Каждый run JSON соответствует ровно одному прогону:
- один алгоритм
- один инстанс
- один seed

### Инвариант 2

`results/logs` и `results/runs` должны быть структурно согласованы по:
- suite
- algorithm
- instance

### Инвариант 3

`results/tables` и `results/history` являются производными и могут быть полностью восстановлены из `results/runs`.

### Инвариант 4

`results/plots` и `results/reports` являются производными и могут быть пересчитаны из `results/tables`, `results/runs` и `results/logs`.

---

## Проверка каталога после прогона

Рекомендуемая последовательность:

```bash
find results/runs -name "*.json" | wc -l
find results/logs -name "*.log" | wc -l
python scripts/aggregate_results.py --search-root results/runs
python scripts/plot_results.py
python scripts/export_excel_report.py --runs-root results/runs --logs-root results/logs --output results/reports/research_report.xlsx
```
