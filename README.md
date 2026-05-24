# LegacyCraft

Personal development fork of **Minecraft Legacy Console Edition** (October 2014 backup, ~TU19).  
This repo tracks UI improvements and custom features added on top of the original PC/Windows 64-bit build.

> **Private repo** — Source code is proprietary to Mojang/Microsoft. This is for personal version control only.

---

## Custom UI System

All custom controls live directly in `Minecraft.Client/` and are registered in `Minecraft.Client.vcxproj`.

---

### `CustomGenericButton` — Button con textura PNG

Botón reutilizable que renderiza una textura normal + hover, con texto centrado.

**Archivos:** `CustomGenericButton.h` / `CustomGenericButton.cpp`

```cpp
CustomGenericButton myButton;

// Botón genérico (textura + tamaño explícitos)
myButton.Setup(L"/Graphics/MyBtn_Norm.png", L"/Graphics/MyBtn_Over.png",
               x, y, width, height);

// Botón de menú principal (usa MainMenuButton_Norm/Over.png, altura automática)
myButton.SetupMenuButton(x, y);

// En tick():
if(myButton.Update(minecraft)) { /* click */ }

// En render():
myButton.Render(minecraft, font, L"Texto", viewport);
```

| Estado | Textura | Texto |
|---|---|---|
| Normal | `*_Norm.png` | Blanco |
| Hover | `*_Over.png` | Amarillo |

**Texturas usadas por `SetupMenuButton`:**
- `Graphics/MainMenuButton_Norm.png` — fondo gris oscuro
- `Graphics/MainMenuButton_Over.png` — fondo azul/violeta

---

### `CustomSlider` — Slider con rango configurable

Control de slider horizontal con fondo de pista, handle arrastrable, overlay de hover y label centrado.

**Archivos:** `CustomSlider.h` / `CustomSlider.cpp`

```cpp
CustomSlider mySlider;

// Tamaño por defecto (600×32), rango 0-100
mySlider.Setup(x, y);

// Rango personalizado
mySlider.Setup(x, y, 200);           // 0-200
mySlider.Setup(x, y, 5);            // 0-5

// Tamaño y rango explícitos
mySlider.Setup(x, y, 480.0f, 40.0f, 100);

// En tick():
bool changed = mySlider.Update(minecraft);

// En render() — muestra "[string]: [valor]%" o "[string]: [valor]/[max]"
mySlider.Render(minecraft, font, IDS_MY_STRING, viewport);

// Leer el valor actual:
int value = mySlider.GetValue();  // 0 … limitMax
```

| Estado | Fondo | Borde | Texto |
|---|---|---|---|
| Normal | `Slider_Track.png` | Ninguno | Blanco |
| Hover | `LeaderboardButton_Over.png` encima | Amarillo 2px | Amarillo |

**Texturas:**
- `Graphics/Slider_Track.png` — pista de fondo (oscuro)
- `Graphics/Slider_Button.png` — handle (el nodo que se arrastra)
- `Graphics/LeaderboardButton_Over.png` — overlay hover (azul)

---

### `CustomGenericBackground` — Fondo panel 9-slice

Fondo reusable que arma un panel a partir de las piezas `Panel*.png` en
`Common/Media/Graphics/PanelsAndTabs/`, respetando el tamano real de cada PNG.
La clase recibe un tamano aproximado y calcula un layout inteligente:

- mantiene esquinas fijas
- repite top/bottom/left/right segun haga falta
- ajusta el centro con segmentos multiples para minimizar el escalado
- soporta tres familias de texturas:
  - `Panel_TL/TM/TR/ML/MM/MR/BL/BM/BR`
  - `Panel_Top/Mid/Bot_*`
  - `Panel_Recess_Top/Mid/Bot_*`

**Archivos:** `CustomGenericBackground.h` / `CustomGenericBackground.cpp`

```cpp
CustomGenericBackground panel;

// Panel clasico con tamano aproximado
panel.Setup(x, y, 720.0f, 420.0f);

// Variante compacta o recessed
panel.Setup(x, y, 320.0f, 180.0f, CustomGenericBackground::eTextureSet_Compact);
panel.Setup(x, y, 320.0f, 180.0f, CustomGenericBackground::eTextureSet_Recessed);

// En render():
panel.Render(minecraft, viewport);
```

**Uso actual:**
- `UIScene_SkinSelectMenu` lo usa como panel pequeno en la esquina inferior derecha para el bloque de `Slim Skin / Skin Delgada`

---

### `CustomCheckbox` — Checkbox con `Tick*.png`

Checkbox simple basado en los PNG de `Common/Media/Graphics`.

**Archivos:** `CustomCheckbox.h` / `CustomCheckbox.cpp`

```cpp
CustomCheckbox myCheckbox;

myCheckbox.Setup(x, y, 0.0f, 0.0f, IDS_MY_OPTION); // 24x24 por defecto + label localizado
// o bien:
myCheckbox.SetStringId(IDS_MY_OPTION);

// En tick():
if(myCheckbox.Update(minecraft))
{
    bool enabled = myCheckbox.IsChecked();
}

// En render():
myCheckbox.Render(minecraft, viewport);
myCheckbox.Render(minecraft, font, viewport);              // usa el stringId guardado
myCheckbox.Render(minecraft, font, L"Mi opcion", viewport); // override manual opcional
```

| Estado | Base | Overlay |
|---|---|---|
| False | `Tickbox_Norm.png` | Ninguno |
| False + Hover | `Tickbox_Over.png` | Ninguno |
| True | `Tickbox_Norm.png` | `Tick.png` |
| True + Hover | `Tickbox_Over.png` | `Tick.png` |

**Texto / localizacion:**
- el checkbox puede guardar un `stringId` interno (`IDS_*`) para tomar el label desde `app.GetString(...)`
- el booleano sigue siendo interno al control y se consulta con `IsChecked()`
- `UIScene_SkinSelectMenu` lo usa para `IDS_SLIM_SKIN`, persistiendo el valor en `UserData_Info::skinSlim`
- el preview del menu aplica el cambio al vuelo, sin reabrir la escena

---

### `Slim Skin` - Integracion completa

La opcion `Slim Skin / Skin Delgada` ya esta conectada de punta a punta:

- `UIScene_SkinSelectMenu` renderiza un `CustomGenericBackground` + `CustomCheckbox` para la opcion
- al hacer toggle, guarda el valor en `UserData_Info::skinSlim`
- `UIControl_PlayerSkinPreview` sincroniza ese bool en cada render para que el cambio se vea al instante en el selector
- `PlayerRenderer` aplica el mismo estado al jugador local en tercera persona y en `renderHand()` para primera persona

**Geometria del brazo:**
- brazo normal: `4x12x4`
- brazo slim: `3x12x4`
- `HumanoidModel::SetSlimArms(...)` rehace `arm0` y `arm1` segun el estado actual
- `ModelPart::resetGeometry()` permite rehacer la caja base sin perder hijos ya adjuntos al brazo (partes extra de skins 4J)

**Archivos clave:**
- `Common/UI/UIScene_SkinSelectMenu.cpp`
- `Common/UI/UIControl_PlayerSkinPreview.cpp`
- `UserData_Info.h` / `UserData_Info.cpp`
- `PlayerRenderer.h` / `PlayerRenderer.cpp`
- `HumanoidModel.h` / `HumanoidModel.cpp`
- `ModelPart.h` / `ModelPart.cpp`

---

## Cambios en `UIScene_MainMenu`

- **6 botones de menú custom** con `CustomGenericButton::SetupMenuButton()`, reemplazando los botones originales de Iggy/Flash
- **Guard de escena activa** — los botones solo se actualizan/renderizan cuando el menú principal tiene el foco, evitando interacción fantasma desde submenús
- **Slider de prueba** en `(0, 0)` para testing en desarrollo (fácil de quitar)

---

## Estructura de archivos relevantes

```
Minecraft.Client/
├── CustomCheckbox.h / .cpp        ← Checkbox con Tickbox/Tick PNG
├── CustomGenericBackground.h / .cpp ← Fondo panel 9-slice con Panel*.png
├── CustomGenericButton.h / .cpp    ← Botón genérico con PNG
├── CustomSlider.h / .cpp           ← Slider con rango y hover
└── Common/
    └── UI/
        └── UIScene_MainMenu.cpp    ← Integración de los controles custom
```

**Tambien relevantes para Slim Skin:**
- `Common/UI/UIScene_SkinSelectMenu.cpp` - overlay del checkbox y persistencia
- `Common/UI/UIControl_PlayerSkinPreview.cpp` - preview en vivo del modelo
- `PlayerRenderer.cpp` - aplicacion real del brazo slim al jugador local
- `HumanoidModel.cpp` - cambio de geometria de `arm0` y `arm1`

---

## Build

- **IDE:** Visual Studio 2012 (v110 toolset)
- **Plataforma:** Windows x64
- **Proyecto:** `Minecraft.Client/Minecraft.Client.vcxproj`

```
Configuración recomendada: Release | x64
```
