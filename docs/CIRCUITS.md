# Composants et circuits — format 1

Un **composant** est un type de pièce (LED, résistance, bouton…) décrit dans
`components/<type>/component.json`. Un **circuit** est un montage sur plaque d'essai décrit dans
`circuits/<nom>.json` : la puce, les composants posés sur des trous et les fils. Les deux formats
sont validés par un JSON Schema Draft 7 (`schemas/component.schema.json`,
`schemas/circuit.schema.json`), puis par des contrôles de cohérence en C++. Les schémas, les
composants de `components/` et les circuits de `circuits/` sont embarqués par CMake : une
installation ne dépend pas du répertoire source. La décision est décrite dans
l'[ADR 0005](adr/0005-composants-et-circuits.md).

Le circuit est calculé en C++ par un modèle électrique (`circuit::Circuit`) : l'interface et le banc de test
voient le même. Trois circuits sont fournis : `arduino-demo` (par défaut), `seven-segment` et `led-buttons`.

```sh
siliconforge --circuit seven-segment firmware/prebuilt/counter.hex   # un circuit embarqué, par son nom
siliconforge --circuit circuits/mon-montage.json firmware.hex       # ou un fichier
siliconforge --circuit none firmware.hex                             # plaque nue
```

Le circuit choisit son microcontrôleur (`chip.mcu`) sauf si `--mcu` est donné. Dans l'interface, la liste
**Circuit**, le bouton **Load circuit…** ou le dépôt d'un fichier JSON changent de circuit ; un circuit invalide est
signalé avec le chemin JSON de la faute. Un microcontrôleur autre que celui du circuit reçoit une plaque nue.

## Trous de la plaque

| Notation | Emplacement |
| --- | --- |
| `e6` | Bande de bornes : rangée `a` à `j`, puis colonne à partir de 1 |
| `T+12`, `T-12` | Rails d'alimentation du haut (5 V et masse), colonne 12 |
| `B+3`, `B-3` | Rails d'alimentation du bas |

Les rangées `f` à `j` d'une colonne sont reliées entre elles, de même que les rangées `a` à `e`.
La puce est à cheval sur le canal central : ses broches 1 à n/2 occupent la rangée `e` à partir de
`chip.column`, les autres la rangée `f` en sens inverse.

## Circuit

```json
{
  "formatVersion": 1,
  "name": "Arduino demo",
  "board": {"type": "breadboard", "columns": 30},
  "chip": {"mcu": "atmega328p", "column": 6},
  "components": [
    {"id": "r-d13", "type": "resistor", "at": {"a": "h15", "b": "h22"}, "properties": {"resistance": 220}},
    {
      "id": "led-d13", "type": "led", "at": {"anode": "f22", "cathode": "f23"},
      "properties": {"color": "red", "forwardVoltage": 1.8}, "view": {"offset": [16, -40]}
    }
  ],
  "wires": [
    {"from": "j23", "to": "T-23", "color": "black"}
  ]
}
```

- `id` identifie le composant dans les tests et l'interface (`led-d13`, `button-d2`).
- `at` associe chaque borne du type à un trou. La broche du MCU n'est pas écrite : elle se déduit
  des trous et des fils.
- `properties` ne contient que les valeurs qui diffèrent des valeurs par défaut du type.
- `view` regroupe des indications de dessin, sans effet sur le comportement.

Contrôles après le schéma, chacun signalé avec le JSON Pointer de la valeur en cause :

| Contrôle | Exemple de chemin |
| --- | --- |
| Puce sur la plaque | `/chip/column` |
| Identifiants uniques | `/components/4/id` |
| Type de composant connu | `/components/0/type` |
| Borne inconnue, borne non posée | `/components/2/at/gate`, `/components/6/at` |
| Trou hors de la plaque | `/components/1/at/b`, `/wires/0/to` |
| Empreinte non respectée | `/components/1/at` |
| Composant dans un rail d'alimentation (seuls les fils y vont) | `/components/1/at/b` |
| Trou déjà occupé | `/wires/0/from` |
| Propriété inconnue, de mauvais type ou hors bornes | `/components/1/properties/resistance` |

## Composant

```json
{
  "formatVersion": 1,
  "type": "led",
  "name": "LED",
  "terminals": [{"name": "anode"}, {"name": "cathode"}],
  "properties": {
    "color": {"type": "string", "default": "red", "enum": ["red", "green", "yellow", "blue"]},
    "forwardVoltage": {"type": "number", "unit": "V", "default": 1.8, "minimum": 0.5, "maximum": 5.0},
    "nominalCurrent": {"type": "number", "unit": "A", "default": 0.01, "minimum": 0.0001, "maximum": 1.0}
  },
  "footprint": {"kind": "fixed", "offsets": {"anode": [0, 0], "cathode": [1, 0]}},
  "model": {"elements": [{"type": "led", "between": ["anode", "cathode"]}]},
  "state": {"brightness": {"type": "number", "minimum": 0, "maximum": 1, "default": 0}}
}
```

- **`terminals`** : les bornes. `role: "bus"` et `bus` (par exemple `spi.sck`) sont réservés aux
  futurs écrans.
- **`properties`** : valeurs réglables par circuit, de type `number` (avec `unit`, `minimum`,
  `maximum`), `string` (avec `enum` facultatif) ou `boolean`. `default` est obligatoire.
- **`footprint`** : l'empreinte au pas de la plaque.

  | `kind` | Signification |
  | --- | --- |
  | `free` | Bornes dans des trous quelconques, rails compris (fil) |
  | `span` | Deux bornes dans la même rangée, écartées de `minimum` à `maximum` trous |
  | `fixed` | Chaque borne à un décalage `[colonne, rangée]` ; le composant peut être retourné le long de la rangée |

- **`model`** : le comportement électrique, comme une liste d'éléments reliant des bornes. Un
  composant simple a un élément ; un afficheur 7 segments en aura huit de type `led`, nommés.

  | Élément | Bornes (`between`) | Propriétés lues | État |
  | --- | --- | --- | --- |
  | `wire` | 2 | — | — |
  | `resistor` | 2 | `resistance` | — |
  | `led` | anode, cathode | `forwardVoltage`, `nominalCurrent` | `brightness` (nombre) |
  | `switch` | 2 | — | `pressed` (booléen) |
  | `potentiometer` | extrémité, curseur, extrémité | `resistance` | `position` (nombre) |

  Un élément `led` peut porter `swapWhen`, le nom d'une propriété booléenne qui inverse anode et cathode quand elle
  est vraie (afficheur à anode commune). Une LED peut aussi déclarer l'état `current` (ampères) et un potentiomètre
  l'état `voltage` (tension du curseur) : le modèle les renseigne.

  Une liste vide décrit une pièce seulement dessinée (quartz). Quand un modèle a plusieurs
  éléments, chacun porte un `name` et son état est préfixé : `a.brightness`.
- **`state`** : ce que le composant expose à l'interface et aux tests. `interactive: true` marque
  ce que l'utilisateur modifie (appui, position). Le type `bytes` est réservé aux états binaires
  volumineux (mémoire d'image d'un écran).

## Modèle électrique

- Les rangées `f` à `j` d'une colonne, les rangées `a` à `e`, et chaque rail forment un nœud ; les fils et les
  boutons appuyés les réunissent en nets.
- Sources idéales : rails `+` (5 V) et `−` (0 V), sorties du MCU, sondes posées depuis l'interface ou le banc.
- Entre les nets : résistances, deux moitiés de potentiomètre, LED (tension de seuil puis 5 Ω), résistance de rappel
  de 35 kΩ d'une entrée qui l'active.
- La luminosité d'une LED est son courant rapporté à `nominalCurrent`, bornée à 1 ; l'interface en reçoit la moyenne
  sur la trame, ce qui rend une PWM comme l'œil la voit.
- Deux sources de tensions différentes sur un même net sont un **court-circuit**, signalé dans un bandeau et par
  `circuit.shorts()`.
- Le modèle est résistif et logique : pas de condensateur, pas de transitoire, pas de courant maximal de sortie.

## Rendu

`components/<type>/render.js` dessine le composant. Le module s'enregistre et reçoit l'état calculé en C++ :

```js
SiliconForge.components.define("led", {
  create(ctx, component) {            // ctx.layer : groupe SVG ; ctx.at : coordonnées des bornes ; ctx.el : création SVG
    const lens = ctx.el("circle", {cx: ctx.at.anode.x, cy: ctx.at.anode.y - 20, r: 8}, ctx.layer);
    return {update(state) { lens.setAttribute("fill-opacity", state.brightness); }};
  },
});
```

`ctx.set(nom, valeur)` transmet une interaction (`pressed`, `position`) au modèle. `component` porte `id`, `type`,
`properties` (valeurs par défaut comprises), `view` et `pin`, la broche du MCU à laquelle le composant est relié à
travers fils et résistances. Un type sans `render.js` est simulé mais pas dessiné.

## En C++

```cpp
#include <siliconforge/testing/Bench.hpp>

testing::Bench bench{mcu::create("atmega328p")};
bench.attach(circuit::builtinDescription("arduino-demo"));   // ou circuit::loadDescription(chemin).value()
bench.load("blink.hex");
bench.runFor(250ms);
CHECK(bench.component("led-d13").brightness() == 1.0);
bench.press("button-d2");                                    // release(), turn("pot-a0", 0.5)
CHECK(bench.circuit().shorts().empty());
```

Sans banc, `circuit::Circuit::create(description)` puis `attach(mcu)` ; appeler `update()` depuis
`Pin::subscribeDrive` de chaque broche.

### Descriptions

```cpp
#include <siliconforge/circuit/Description.hpp>
using namespace siliconforge;

auto circuit = circuit::loadDescription("circuits/mon-montage.json");
if (!circuit) {
    // circuit.error().path : JSON Pointer ; circuit.error().message : diagnostic
}

circuit::Library library = circuit::Library::builtin();   // types embarqués
library.addFile("mes-composants/buzzer/component.json");   // puis les vôtres
auto autre = circuit::loadDescription("circuits/alarme.json", library);
```

`circuit::validateComponent(json)` et `circuit::validateCircuit(json, library)` valident sans
lire de fichier ; `circuit::builtinDescription("arduino-demo")` rend le circuit de démonstration.
Cible CMake : `SiliconForge::circuit`.

## Ajouter un type de composant

1. Créer `components/<type>/component.json` ; le nom du répertoire est le `type`.
2. Ajouter `components/<type>/render.js` pour le dessiner.
3. Relancer CMake : les deux fichiers sont embarqués et la description validée par les tests (`ctest -R component`).
4. Si le comportement n'est pas exprimable avec les éléments ci-dessus, il faut un nouvel élément dans
   `src/circuit/Circuit.cpp`.
