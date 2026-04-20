# План внедрения текстурирования в `software_renderer`

## Цели этапа

1. Сохранить **наглядность прохождения данных** (буфер → IA → VS → rasterizer → PS → кадр).
2. Сохранить стиль API, похожий на D3D11 DeviceContext, но без лишней магии.
3. Сделать первый инкремент минимальным и наблюдаемым в отладчике.

## Наблюдения по текущей архитектуре

- `InputLayout` и `VertexInputView` уже поддерживают семантику `TEXCOORD0`, но текущие `VSOutput/PSInput` несут только цвет и глубину.
- Сейчас есть bind constant buffer для VS/PS, но нет bind-point для текстур/самплеров.
- Растеризация уже имеет перспективно-корректную интерполяцию (через `1/w`) для цвета; это удобно расширить на UV.

## Предлагаемая дорожная карта

### Шаг 1. Минимальный вертикальный срез (1 текстура, 1 UV, point sampling)

**Цель:** получить первый треугольник/квадрат с текстурой через полный конвейер.

Сделать:
- Добавить `Texture2D` ресурс:
  - `width`, `height`, `format` (на старте `R8G8B8A8_UNORM`),
  - хранилище `std::vector<glm::vec4>` или `std::vector<uint32_t>`.
- Добавить `SamplerState`:
  - Address mode: `Clamp`, `Wrap`.
  - Filter: только `Point`.
- Добавить bind в PS stage:
  - `setShaderResource(size_t slot, std::shared_ptr<Texture2D>)`,
  - `setSampler(size_t slot, SamplerState)`.
- Расширить интерфейсы данных:
  - `VSOutput` добавить `glm::vec2 texcoord`.
  - `PSInput` добавить `glm::vec2 texcoord`.
- В `rasterizeTri` добавить перспективно-корректную интерполяцию UV (по той же схеме, что и для цвета).
- В `ShaderContext` дать доступ к PS-текстурам и sampler-ам (`psTexture(slot)`, `psSampler(slot)`).
- Добавить helper в текстуре: `samplePoint(uv, sampler)`.

Критерий готовности:
- Текстурированный примитив рисуется стабильно,
- UV корректны при перспективе,
- В отладчике видно: чтение `TEXCOORD0` во VS, интерполяция UV, выбор texel в PS.

### Шаг 2. Демонстрационный контент и диагностика

**Цель:** сделать этап учебным и проверяемым.

Сделать:
- Новая сцена `TexturedQuadScene` (или расширить существующую):
  - вершины с `POSITION0 + COLOR0 + TEXCOORD0`,
  - шахматная/градиентная процедурная текстура,
  - переключение режимов `Clamp/Wrap`, `Point`.
- Debug-режимы в PS:
  - вывод `uv` как цвет,
  - вывод выбранного texel-координатного паттерна,
  - смешивание `vertex color * texture`.

Критерий готовности:
- можно визуально и через debugger понять, где ломается путь данных.

### Шаг 3. Улучшение качества с сохранением наглядности

**Цель:** показать отличие фильтрации и LOD без чрезмерной сложности.

Сделать:
- `Linear` фильтрация (bilinear).
- Производные UV (`ddx/ddy`) на 2x2 пиксельных квадах (упрощённо) и выбор mip level.
- Mipmap chain (можно CPU-генерацию при создании текстуры).

Критерий готовности:
- демонстрация aliasing vs filtering,
- код по-прежнему читаем и трассируем.

### Шаг 4. Расширение API к “D3D-like” модели

Сделать:
- Явно разделить ресурсы и views:
  - `Texture2D` (resource),
  - `ShaderResourceView` (view) — можно отложить как thin-wrapper.
- Добавить ограничение/валидацию slot-ов и понятные assert-сообщения.
- Опционально подготовить задел под normal map и несколько текстурных слотов.

## Предложение по структуре данных (минимум)

- `swrTexture.h/.cpp`:
  - `class Texture2D` (create/upload/read/sample).
  - `enum class TextureAddressMode`, `enum class TextureFilter`.
  - `struct SamplerState`.
- `swrDevice.h/.cpp`:
  - расширение `PSStage` на texture/sampler slots,
  - расширение `ShaderContext` доступом к ним,
  - расширение `VSOutput/PSInput` полем `texcoord`.

## Риски и как снизить

1. **Путаница с перспективной коррекцией UV.**
   - Решение: переиспользовать существующий паттерн для цвета (`attrib * invW / sum`).
2. **UV вне диапазона [0..1].**
   - Решение: централизовать в `sample*` логику `Clamp/Wrap`.
3. **Падение на пустых слотах текстур.**
   - Решение: дефолт `nullptr` + fallback-цвет в PS (например magenta).
4. **Сложность API для учебного проекта.**
   - Решение: сначала only-PS binding, без SRV/RTV/DSV-иерархии.

## Рекомендуемый порядок реализации

1. `Texture2D + SamplerState`.
2. Поля UV в `VSOutput/PSInput`.
3. Интерполяция UV в `rasterizeTri`.
4. PS binding и `ShaderContext` доступ к texture/sampler.
5. Тестовая сцена с процедурной текстурой.
6. Debug-режимы визуализации UV.

## Что отложить на следующий этап

- Анизотропную фильтрацию.
- Полноценный `ShaderResourceView` API для подресурсов.
- Гамма-коррекцию/sRGB pipeline.
