# Composants et circuits — format 1

Un **composant** est un type de pièce (LED, résistance, bouton…) décrit dans
`components/<type>/component.json`. Un **circuit** est un montage sur plaque d'essai décrit dans
`circuits/<nom>.json` : la puce, les composants posés sur des trous et les fils. Les deux formats
sont validés par un JSON Schema Draft 7 (`schemas/component.schema.json`,
`schemas/circuit.schema.json`), puis par des contrôles de cohérence en C++. Les schémas, les
composants de `components/` et `circuits/arduino-demo.json` sont embarqués par CMake : une
installation ne dépend pas du répertoire source. La décision est décrite dans
l'[ADR 0005](adr/0005-composants-et-circuits.md).

> État : les descriptions sont chargées et validées (#18) et le circuit est calculé en C++, visible
> du banc de test (#19). Le rendu par type (#20) et le chargement d'un circuit par l'application
> (#21) suivent. En attendant, la vue Breadboard dessine encore la section `breadboard` de la
> description du MCU ; un test vérifie qu'elle place les mêmes pièces aux mêmes trous que
> `circuits/arduino-demo.json`.

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

  Une liste vide décrit une pièce seulement dessinée (quartz). Quand un modèle a plusieurs
  éléments, chacun porte un `name` et son état est préfixé : `a.brightness`.
- **`state`** : ce que le composant expose à l'interface et aux tests. `interactive: true` marque
  ce que l'utilisateur modifie (appui, position). Le type `bytes` est réservé aux états binaires
  volumineux (mémoire d'image d'un écran).

## En C++

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

## Modèle électrique

`circuit::Circuit` calcule le montage en C++ : les tests voient le même circuit que l'utilisateur.
Le modèle est logique et résistif, sans simulation SPICE.

1. **Nets.** Les trous d'une bande, les rails, les fils et les boutons enfoncés forment des nets.
   Les broches de la puce rejoignent le net de leur trou quand elle est posée (`attach`).
2. **Sources.** Les rails `+` sont à la tension d'alimentation du MCU, les rails `−` à 0 V. Une
   broche en sortie fixe son net à l'un ou l'autre.
3. **Court-circuit.** Deux sources de tensions différentes sur un même net produisent un
   `Fault::ShortCircuit` (sources, cycle), une fois par apparition, dans `circuit.faults()`.
4. **Potentiomètre.** Entre deux nets fixés, le curseur est à `Va + position × (Vb − Va)`.
5. **LED.** En série avec des résistances entre deux nets de tension connue :
   `I = (V₊ − V₋ − ΣVf) / ΣR`, et `brightness = I / nominalCurrent`, bornée à 1. Une LED à l'envers
   reste éteinte. `meanBrightness()` moyenne la luminosité sur le temps simulé écoulé depuis
   l'appel précédent : c'est ce que l'œil voit d'une PWM.
6. **Tirages.** Un net sans source relié par des résistances à des nets connus prend la tension
   du diviseur qu'elles forment (résistance de rappel).
7. **Entrées du MCU.** Chaque broche reçoit la tension de son net (`Pin::setVoltage`), ou est
   relâchée si rien ne le fixe ; le pull-up interne joue alors son rôle.

Limites assumées : les mailles et les LED en parallèle sur une même résistance ne sont pas
résolues (ces LED restent éteintes) ; un net où une LED est branchée n'a pas de tension de tirage ;
le pull-up interne n'alimente rien ; une LED sans résistance est à pleine luminosité sans défaut
signalé. Le circuit n'est recalculé qu'aux changements de broche : avec `demo.hex` (PWM à 7,8 kHz),
l'émulation à vitesse maximale est ralentie d'environ 9 % ; sans circuit attaché, rien ne change.

### Depuis le banc de test

```cpp
#include <siliconforge/testing/Bench.hpp>

testing::Bench bench{mcu::create("atmega328p")};
bench.attach(circuit::Circuit::builtin("arduino-demo"));   // ou bench.attach("circuits/x.json")
bench.load("blink.hex");

bench.runFor(600ms);
CHECK(bench.component("led-d13").brightness() == 1.0);
bench.press("button-d2");                                  // D2 passe à la masse
bench.component("pot-a0").set("position", 0.5);            // 2,5 V sur A0
CHECK(bench.circuit().faults().empty());
```

`Component::state("nom")` lit n'importe quelle entrée de `state` ; `set` n'accepte que les états
interactifs. `Circuit::connected("f15", "h15")` et `Circuit::voltage("g3")` interrogent les nets.

## Ajouter un type de composant

1. Créer `components/<type>/component.json` ; le nom du répertoire est le `type`.
2. Relancer CMake : le fichier est embarqué et validé par les tests (`ctest -R component`).
3. Si le comportement n'est pas exprimable avec les éléments ci-dessus, il faut un nouvel
   élément dans le modèle C++ (`src/circuit/Circuit.cpp`).
