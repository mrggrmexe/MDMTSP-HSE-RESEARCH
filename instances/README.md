# Instances

Папка содержит входные данные для задачи MDMTSP.

## Формат JSON

Каждый файл инстанса имеет следующую структуру:

```json
{
  "name": "instance_name",
  "return_to_depot": true,
  "salesman_count": 4,
  "depots": [
    { "x": 0.0, "y": 0.0 },
    { "x": 10.0, "y": 0.0 }
  ],
  "customers": [
    { "x": 1.0, "y": 1.0 },
    { "x": 2.0, "y": 2.0 }
  ]
}