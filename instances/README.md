# `instances/` README

## Назначение

Каталог `instances/` содержит входные JSON-инстансы для solver-а и исследовательского пайплайна.

Инстансы используются:
- `scripts/run_suite.py`
- `scripts/check_instances.py`
- `scripts/plot_results.py`
- `scripts/visualize_solution.py`

---

## Структура каталога

Ожидаемая базовая структура:

```text
instances/
├── research/
│   ├── group_01/
│   ├── group_02/
│   ├── group_03/
│   ├── group_04/
│   ├── group_05/
│   ├── group_06/
│   ├── group_07/
│   ├── group_08/
│   ├── group_09/
│   └── group_10/
└── ...
```

Основной рабочий набор находится в `instances/research/`.

Группы:
- `group_01` … `group_10`

Типовой workflow:
- генерация: `scripts/generate_research_instances.py`
- валидация: `scripts/check_instances.py`
- запуск suite: `scripts/run_suite.py`

---

## Соглашения по именованию файлов

### Основной шаблон

Исследовательские инстансы именуются по шаблону:

```text
gXX_iYY_cN_dK_mM_<type>.json
```

где:
- `gXX` — номер группы
- `iYY` — номер инстанса внутри группы
- `cN` — число клиентов (`customers`)
- `dK` — число депо (`depots`)
- `mM` — число коммивояжёров (`salesmen`)
- `<type>` — тип распределения

Примеры:
- `g01_i01_c10_d1_m3_random.json`
- `g06_i03_c10000_d7_m39.json`
- `g07_i02_c25000_d5_m26_random.json`

### Поля, кодируемые в имени файла

| Сегмент | Значение |
|---|---|
| `gXX` | группа |
| `iYY` | индекс инстанса |
| `cN` | `customer_count` |
| `dK` | `depot_count` |
| `mM` | `salesman_count` |

`check_instances.py` может сопоставлять filename с содержимым JSON и поднимать warning/error при несоответствии.

---

## Типы инстансов

В исследовательском наборе используются различные типы пространственного распределения.

Подтверждённые типы в текущих данных и агрегатах:
- `random`
- `clustered`
- `grid`
- `line`
- `adversarial`

Тип инстанса может:
- присутствовать в имени файла,
- присутствовать в JSON-поле `type`,
- использоваться downstream-агрегацией в `results/tables/*instance_type_summary.csv`.

---

## Поддерживаемые форматы JSON

`instances/` допускает два формата.

### 1. Modern schema

Рекомендуемый формат.

Пример:

```json
{
  "name": "g01_i01_c10_d1_m3_random",
  "type": "euclidean",
  "seed": 42,
  "depots": [
    { "id": 0, "x": 0.0, "y": 0.0, "salesmen": 3 }
  ],
  "customers": [
    { "id": 1, "x": 1.0, "y": 1.0 },
    { "id": 2, "x": 2.0, "y": 2.0 }
  ]
}
```

Поля верхнего уровня:
- `name: string`
- `type: string`
- `seed: integer` — опционально, но рекомендуется
- `depots: array<object>`
- `customers: array<object>`

#### `depots[]`

Обязательные поля:
- `id: integer`
- `x: number`
- `y: number`
- `salesmen: integer`

#### `customers[]`

Обязательные поля:
- `id: integer`
- `x: number`
- `y: number`

Семантика:
- `id` должны быть уникальны глобально по всем depot/customer узлам.
- общее число коммивояжёров вычисляется как сумма `depots[i].salesmen`.

### 2. Legacy schema

Поддерживается для совместимости.

Пример:

```json
{
  "name": "legacy_case",
  "depots": [
    { "x": 0.0, "y": 0.0 },
    { "x": 10.0, "y": 0.0 }
  ],
  "customers": [
    { "x": 1.0, "y": 0.0 },
    { "x": 2.0, "y": 0.0 }
  ],
  "salesman_count": 3,
  "return_to_depot": true
}
```

Поля:
- `name: string`
- `depots: array<object{x,y}>`
- `customers: array<object{x,y}>`
- `salesman_count: integer`
- `return_to_depot: boolean` — опционально

Семантика:
- `salesman_count` задаётся глобально.
- явных `id` нет; downstream-система должна назначать их сама.

---

## Формальные требования к данным

### Общие требования

Для любого инстанса:
- JSON должен быть корректным.
- `depots` и `customers` должны существовать и быть массивами.
- координаты должны быть конечными числами.
- число депо должно быть `>= 1`.
- число клиентов должно быть `>= 1`.
- число коммивояжёров должно быть `>= 1`.

### Дополнительные требования для modern schema

- все `id` уникальны;
- все `salesmen > 0` на уровне депо;
- суммарное число `salesmen` корректно вычисляется;
- `type`, если указан, должен быть допустимым для consumer-ов.

### Дополнительные требования для legacy schema

- `salesman_count >= 1`;
- каждый depot/customer должен содержать `x` и `y`.

---

## Семантика расстояний

Текущая инфраструктура ориентирована на евклидовые координаты.

Допустимое значение `type` в modern schema:
- `euclidean`

Практика:
- если используется `type`, задавать `type: "euclidean"`;
- значения вроде `random`, `grid`, `clustered`, `line`, `adversarial` должны интерпретироваться как класс распределения, а не как distance type, если они хранятся отдельно от solver-facing `type`.

Рекомендуемое разделение:
- `type: "euclidean"` — метрика/геометрия
- дополнительное поле или filename suffix — класс генерации (`random`, `clustered`, ...)

---

## Семантика узлов

Во внутренней нумерации solver-а:
- сначала идут депо,
- затем клиенты.

Типовая схема:
- depot nodes: `0 ... depot_count - 1`
- customer nodes: `depot_count ... depot_count + customer_count - 1`

Для modern schema это обычно согласуется с JSON `id`.
Для legacy schema `id` назначаются производно.

---

## Связь с группами `research/`

### Группы

Генератор исследовательских инстансов формирует 10 групп.
Каждая группа соответствует диапазону размеров или сценарию генерации.

Ожидаемая структура:

```text
instances/research/
├── group_01/
├── group_02/
├── group_03/
├── group_04/
├── group_05/
├── group_06/
├── group_07/
├── group_08/
├── group_09/
└── group_10/
```

### Практический memory budget

Текущая solver-инфраструктура использует distance matrix с квадратичной памятью.

Практическое следствие:
- группы с экстремально большими `customer_count` не всегда подходят для стандартного запуска;
- для рабочих серий используется suite `research_until_25000`, который ограничивает запуск группами до `25000` клиентов.

---

## Связь с `configs/suites/*.json`

Каталог `instances/` используется suite-конфигами через:
- `instance_roots`
- `include_globs`
- `exclude_globs`

Примеры:
- `instance_roots = ["instances/research"]`
- `instance_roots = ["instances/research/group_03"]`

Семантика:
- `run_suite.py` рекурсивно находит JSON-инстансы под указанными корнями;
- `include_globs` и `exclude_globs` фильтруют фактический набор файлов.

---

## Использование в скриптах

### Валидация

```bash
python scripts/check_instances.py instances/research
```

### Валидация с отчётами

```bash
python scripts/check_instances.py \
  instances/research \
  --report-csv results/reports/instance_check.csv \
  --report-jsonl results/reports/instance_check.jsonl
```

### Запуск suite по всему исследовательскому набору

```bash
python scripts/run_suite.py \
  --config configs/suites/research_all.json \
  --executable build/bin/mdmtsp
```

### Запуск по одной группе

```bash
python scripts/run_suite.py \
  --config configs/suites/research_group_03.json \
  --executable build/bin/mdmtsp
```

---

## Рекомендации по созданию новых инстансов

### Для нового инстанса в modern schema

Обязательные шаги:
1. задать `name`;
2. задать `type: "euclidean"` или совместимое solver-facing значение;
3. задать уникальные `id` всем depots/customers;
4. задать `salesmen` на уровне депо;
5. сохранить файл в корректный каталог;
6. прогнать `check_instances.py`.

### Для исследовательского набора

Рекомендуется:
- сохранять файл в `instances/research/group_XX/`
- соблюдать шаблон имени `gXX_iYY_cN_dK_mM_<class>.json`
- фиксировать `seed`
- фиксировать класс распределения в имени файла

---

## Типовые ошибки

### Некорректный `instance_path` в `results/runs`

Причина:
- в run-артефакт попал абсолютный путь с другой машины.

Пример:
```text
/Users/<user>/.../instances/research/group_07/...
```

Решение:
- сохранять в run JSON repo-relative путь:
  `instances/research/group_07/...`
- использовать обновлённый `visualize_solution.py`, который умеет резолвить такие пути через `repo_root/instances`

### Несоответствие filename и JSON-полей

Причина:
- в имени файла `c25000_d5_m26`, а в JSON другие значения.

Решение:
- привести в соответствие filename и содержимое.

### Неподдерживаемый `type`

Причина:
- solver/validator ожидает `euclidean`, а в `type` записан класс распределения.

Решение:
- отделить метрику от класса генерации.

### Повреждённый JSON

Проверка:
```bash
python -m json.tool instances/research/group_01/example.json > /dev/null
```

---

## Валидация каталога

### Проверить, что все JSON парсятся

```bash
find instances -name "*.json" -print0 | while IFS= read -r -d '' f; do
  python -m json.tool "$f" > /dev/null || exit 1
done
```

### Запустить полный validator

```bash
python scripts/check_instances.py instances
```

### Проверить количество инстансов

```bash
find instances -name "*.json" | wc -l
```

### Проверить конкретную группу

```bash
find instances/research/group_03 -name "*.json" | sort
```

---

## Минимальный инстанс (modern schema)

```json
{
  "name": "minimal_case",
  "type": "euclidean",
  "seed": 1,
  "depots": [
    { "id": 0, "x": 0.0, "y": 0.0, "salesmen": 1 }
  ],
  "customers": [
    { "id": 1, "x": 1.0, "y": 0.0 }
  ]
}
```

## Минимальный инстанс (legacy schema)

```json
{
  "name": "minimal_legacy_case",
  "depots": [
    { "x": 0.0, "y": 0.0 }
  ],
  "customers": [
    { "x": 1.0, "y": 0.0 }
  ],
  "salesman_count": 1,
  "return_to_depot": true
}
```

---

## Минимальный рабочий набор каталога

Для рабочего исследовательского цикла достаточно:
- `instances/research/group_01/ ... group_07/`
- валидные JSON-инстансы
- согласованные filename и metadata
- корректные suite-конфиги, ссылающиеся на нужные `instance_roots`

Для полного генератора:
- `group_01 ... group_10`

---

## Технические требования к git-версии данных

Рекомендуется:
- не менять уже использованные в экспериментах инстансы без изменения имени файла;
- при изменении структуры/координат создавать новый файл;
- не перезаписывать silently инстансы, уже фигурирующие в `results/runs`;
- при необходимости фиксировать генератор и seed, чтобы восстановить набор.

---

## Проверка после генерации

Рекомендуемая последовательность:

```bash
python scripts/generate_research_instances.py
python scripts/check_instances.py instances/research
python scripts/run_suite.py --config configs/suites/research_group_03.json --executable build/bin/mdmtsp
```
