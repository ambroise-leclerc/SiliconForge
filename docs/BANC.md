# Banc programmable : scénarios et `siliconforge-bench`

Un scénario décrit un essai de banc dans un fichier versionné avec le projet : le circuit et le firmware, l’état
initial, puis les stimuli, les attentes bornées et les mesures avec leurs valeurs attendues. `siliconforge-bench`
l’exécute sans fenêtre et conserve les preuves de l’essai. La décision est décrite dans
l’[ADR 0010](adr/0010-banc-programmable.md) ; ce document couvre le premier incrément, sur l’ATmega328P simulé.

## Un exemple

[`scenarios/pwm-setpoint.json`](../scenarios/pwm-setpoint.json) vérifie le firmware
[`pwm_setpoint.c`](../firmware/src/pwm_setpoint.c) sur le circuit de démonstration : la PWM de D6 suit la consigne
choisie par le bouton D2 (25 % relâché, 75 % enfoncé) et la LED D13 acquitte la consigne haute.

```json
{
  "formatVersion": 1,
  "name": "PWM setpoint on D6",
  "circuit": "../circuits/arduino-demo.json",
  "firmware": "../firmware/prebuilt/pwm_setpoint.hex",
  "initial": {"components": {"button-d2": {"pressed": false}}},
  "steps": [
    {"label": "Let the firmware configure Timer0", "wait": "1ms"},
    {"measure": {"id": "duty-released", "pin": "D6", "quantity": "dutyCycle", "window": "1ms", "expect": {"value": 25, "tolerance": 0.5}}},
    {"label": "Hold the button", "at": "5ms", "set": {"component": "button-d2", "state": "pressed", "value": true}},
    {"label": "The firmware acknowledges", "waitFor": {"pin": "D13", "state": "high"}, "timeout": "1ms"},
    {"measure": {"id": "duty-held", "pin": "D6", "quantity": "dutyCycle", "window": "1ms", "expect": {"value": 75, "tolerance": 0.5}}}
  ]
}
```

```console
$ ./build/apps/siliconforge-bench/siliconforge-bench run scenarios/pwm-setpoint.json
PWM setpoint on D6: pass
  [done]     0 wait     Let the firmware configure Timer0
  [done]     1 measure  Released setpoint — duty-released = 25 %
  ...
  ok   duty-held dutyCycle on D6: 75.0032 % (expected 74.5 to 75.5 %)
Simulated 0.0070518125 s on ATmega328P at 16000000 Hz
Report: .siliconforge/runs/pwm-setpoint/report.json
Trace:  .siliconforge/runs/pwm-setpoint/trace.vcd
```

## Le format

Le schéma [`schemas/scenario.schema.json`](../schemas/scenario.schema.json) fait foi. Les chemins `circuit` et
`firmware` sont relatifs au fichier du scénario ; la puce du circuit choisit le microcontrôleur. Le firmware est un
fichier Intel HEX ou ELF.

Les durées sont du temps simulé : `"250us"`, `"1.5 ms"`, `"2s"`, ou un nombre de cycles d’horloge, `"16000 cycles"`.
Unités acceptées : `ns`, `us`, `ms`, `s`, `cycles`. Aucune durée ne dépasse une heure simulée.

`initial` est appliqué avant le démarrage du firmware : états des composants (`{"button-d2": {"pressed": false}}`) et
sondes, sources de tension idéales sur des broches (`{"A0": 2.5}`).

Les étapes s’exécutent dans l’ordre. Chacune peut porter un `label` et un instant de départ `at`, compté depuis le
reset ; sinon elle commence à la fin de la précédente.

## Le temps d’un essai

Les étapes suivent une chronologie planifiée, en cycles : une attente `wait` ou une fenêtre de mesure finit
exactement `durée` cycles après son début, et `at` place une étape sur un cycle donné. Le cœur émulé agit entre deux
instructions, comme ses périphériques : quand un instant planifié tombe au milieu d’une instruction de plusieurs
cycles, l’instruction se termine d’abord. Ce dépassement ne décale pas la chronologie.

- Un stimulus (`set`) prend effet à la première frontière d’instruction à partir de son instant planifié, au plus
  quelques cycles plus tard. Le rapport donne les deux : `start`, l’instant planifié, et `applied`, le cycle où le
  stimulus a pris effet.
- Une mesure porte exactement sur `[début, début + fenêtre]` : les changements de la broche sont enregistrés au cycle
  où ils se produisent, et ceux du dépassement sont exclus.
- Une attente `waitFor` est évaluée à chaque changement d’une broche, du circuit ou d’un port série, au cycle de ce
  changement, et non par échantillonnage : un niveau atteint puis quitté en deux cycles la satisfait. Son instant de
  fin est le cycle où la condition a été remplie. Seul l’état d’un composant atteint pendant le dépassement initial,
  qui n’est pas enregistré, est daté de la frontière d’instruction suivante.

| Étape | Effet |
| --- | --- |
| `{"wait": "2ms"}` | Avance le temps simulé |
| `{"set": {"component": "button-d2", "state": "pressed", "value": true}}` | Change un état interactif d’un composant, comme l’utilisateur sur le breadboard |
| `{"set": {"probe": "A0", "volts": 2.5}}` | Pose une sonde sur une broche ; `null` la retire |
| `{"waitFor": {"pin": "D13", "state": "high"}, "timeout": "1ms"}` | Attente bornée d’un niveau (`high`, `low`, `floating`) |
| `{"waitFor": {"serial": "ready", "port": 0}, "timeout": "100ms"}` | Attente bornée d’un texte émis sur un port série depuis le début de l’étape |
| `{"waitFor": {"component": "led-d13", "state": "brightness", "above": 0.5}, "timeout": "1s"}` | Attente bornée d’un état de composant (`above`, `below`) |
| `{"measure": {"id": "duty", "pin": "D6", "quantity": "dutyCycle", "window": "1ms", "expect": {...}}}` | Laisse tourner le banc pendant la fenêtre puis mesure sur les fronts capturés |

Les grandeurs mesurées sont `dutyCycle` (%), `frequency` (Hz) et `period` (s). `expect` vaut
`{"value": 25, "tolerance": 0.5}` ou `{"min": 24.5, "max": 25.5}`, dans l’unité de la grandeur. Sans `expect`, la
mesure est informative et n’influe pas sur le verdict.

## Mesures

Les mesures portent sur les changements de la broche, datés au cycle près comme dans les
[instruments](INSTRUMENTS.md). Un front montant passe de l’état bas à l’état haut. La fréquence et la période moyennes
sont calculées entre le premier et le dernier front montant de la fenêtre. Le rapport cyclique est le temps passé à
l’état haut sur ces mêmes périodes complètes. Une broche qui ne change pas pendant la fenêtre a un rapport cyclique de
0 % ou 100 %.

La mesure est impossible quand la broche flotte pendant la fenêtre, même brièvement (une haute impédance n’a pas de
niveau : ni son temps ni ses transitions ne comptent comme un signal), ou quand une fréquence ne dispose pas de deux
fronts montants. La valeur est alors absente, l’explication figure dans le rapport, et une mesure qui a un attendu
échoue.

## Ce qui est refusé avant l’essai

`siliconforge-bench check` et `run` vérifient tout le scénario avant d’exécuter la moindre instruction. Chaque refus
nomme le fichier fautif, le JSON Pointer de la faute et sa cause :

| Cause | Exemples |
| --- | --- |
| `unreadable` | Scénario, circuit ou firmware illisible ; texte qui n’est pas du JSON ; HEX corrompu |
| `version` | `formatVersion` inconnu de ce moteur |
| `schema` | Étape mal formée, durée sans unité, identifiant de mesure en double |
| `wiring` | Circuit invalide, puce qui ne tient pas sur la carte, sonde sur une broche absente du breadboard |
| `resource` | Microcontrôleur inconnu, composant, état, broche ou port série absent, état non modifiable de l’extérieur |
| `capacity` | Valeur hors des bornes d’un composant, sonde au-delà de l’alimentation, tolérance plus fine que la résolution de la fenêtre (100 % / nombre de cycles), fenêtre trop courte pour deux fronts montants à la fréquence attendue, fréquence au-delà de la moitié de l’horloge, durée de plus d’une heure |
| `schedule` | Étape placée (`at`) avant la durée minimale des étapes qui la précèdent |

## La commande

```text
siliconforge-bench run [--out <répertoire>] [--json] <scénario.json>
siliconforge-bench check <scénario.json>
```

| Code de sortie | Signification |
| --- | --- |
| 0 | Réussite : chaque attente a abouti à temps et chaque mesure respecte son attendu |
| 1 | Échec : une mesure manque son attendu, une attente atteint son échéance, ou le MCU s’arrête (opcode invalide…) |
| 2 | Banc incompatible : le scénario ne peut pas s’exécuter (causes ci-dessus) |
| 3 | Erreur d’utilisation ou d’écriture des preuves |

Après une attente échouée, les étapes suivantes ne sont pas exécutées (`skipped`) : elles partiraient d’un état
imprévu. Une mesure hors tolérance n’arrête pas l’essai, les suivantes sont prises.

## Les preuves d’un essai

`run` écrit dans `--out`, par défaut `.siliconforge/runs/<nom du fichier>/` dans le répertoire courant (ignoré par Git) :

- `report.json` : verdict ; version du moteur et du format ; empreinte SHA-256 et chemin du scénario, du circuit et du
  firmware ; microcontrôleur et horloge ; chaque étape avec son issue, ses instants planifiés de début et de fin, le
  cycle où un stimulus a pris effet (`applied`) (en cycles et en
  secondes) et son détail ; chaque mesure avec sa valeur, son unité, sa fenêtre, le nombre de fronts montants et
  l’attendu ; la sortie série ; la cause d’une incompatibilité.
- `trace.vcd` : toutes les broches depuis le reset, à ouvrir dans GTKWave, PulseView ou Surfer. L’échelle de temps est
  la plus grande échelle valide (1, 10 ou 100 s, ms, µs, ns, ps ou fs) qui divise la période d’horloge : 100 ps à
  16 MHz, soit 625 unités par cycle. Une période qui n’est pas un nombre entier de femtosecondes donne une échelle de
  1 fs et des instants arrondis. `report.json` indique l’échelle dans `trace.timescale`.

La trace et le rapport sont écrits même quand une assertion échoue. Une ancienne `trace.vcd` est toujours supprimée
du répertoire, même quand le nouvel essai n’en produit pas (scénario incompatible) : chaque fichier présent
correspond au rapport. Ils ne contiennent ni horodatage ni durée
d’exécution réelle : deux essais sur les mêmes entrées produisent des fichiers identiques, échéance dépassée
comprise.

## Depuis C++

La bibliothèque `SiliconForge::scenario` expose le même moteur
([`Scenario.hpp`](../include/siliconforge/scenario/Scenario.hpp)) :

```cpp
auto prepared = scenario::Scenario::prepare("scenarios/pwm-setpoint.json");
if (!prepared)
    return report(prepared.error());     // cause, file, JSON Pointer, message
const auto result = (*prepared)->run();  // repeatable: each run starts from a fresh MCU and circuit
const auto json   = scenario::toJson(result, "trace.vcd");
```

## Limites

- Les stimuli disponibles sont les états interactifs des composants (boutons, potentiomètres) et les sondes de
  tension. L’alimentation pilotable, la charge variable, le GBF et l’écran externe font l’objet des stories #62, #63 et
  #64.
- Les mesures portent sur une broche à la fois ; pas encore de tension moyenne, de niveau d’un nœud du circuit ni de
  décodage UART dans un scénario.
- Le premier incrément est qualifié sur l’ATmega328P simulé. La reprise de l’essai dans la fenêtre est l’objet de #60.
