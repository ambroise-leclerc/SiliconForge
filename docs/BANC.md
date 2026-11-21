# Banc programmable : scénarios et `siliconforge bench`

Un scénario décrit un essai de banc dans un fichier versionné avec le projet : le circuit et le firmware, l’état
initial, puis les stimuli, les attentes bornées et les mesures avec leurs valeurs attendues. `siliconforge bench`
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
$ ./build/apps/siliconforge/siliconforge bench run scenarios/pwm-setpoint.json
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
  stimulus a pris effet. Les étapes suivantes restent sur la chronologie planifiée : une attente qui suit un stimulus
  compte son délai depuis l’instant planifié, et un délai plus court que l’instruction en cours expire avant que le
  stimulus ait pu agir.
- Une mesure porte exactement sur la fenêtre fermée `[début, début + fenêtre]` : un front situé sur l’une des deux
  bornes compte, et ceux du dépassement sont exclus. Les changements d’une broche sont datés du cycle où le cœur les
  produit ; un front engendré par un périphérique (comparaison d’un timer) est daté de son cycle prévu, même pendant
  une instruction de plusieurs cycles. Les événements restent traités entre les instructions
  ([ADR 0002](adr/0002-modele-temporel.md)).
- Une attente `waitFor` est évaluée à chaque changement d’une broche, du circuit ou d’un port série, au cycle de ce
  changement, et non par échantillonnage : un niveau atteint puis quitté en deux cycles la satisfait. Son instant de
  fin est le cycle où la condition a été remplie. Les niveaux des broches, les octets émis et les états de composant
  surveillés par une attente sont enregistrés à chaque changement : une condition déjà remplie au début de l’étape
  l’est à son instant planifié, même quand le cœur l’a dépassé.

| Étape | Effet |
| --- | --- |
| `{"wait": "2ms"}` | Avance le temps simulé |
| `{"set": {"component": "button-d2", "state": "pressed", "value": true}}` | Change un état interactif d’un composant, comme l’utilisateur sur le breadboard |
| `{"set": {"component": "gbf", "samples": [0, 5, 5, 0], "period": "4ms"}}` | Programme l’onde arbitraire d’un générateur de signaux (`signal-generator`) et la sélectionne : 2 à 4096 échantillons en volts (±20 V), chacun tenu `period / nombre` ; voir [Générateur de signaux](CIRCUITS.md#générateur-de-signaux) |
| `{"set": {"probe": "A0", "volts": 2.5}}` | Pose une sonde sur une broche ; `null` la retire |
| `{"waitFor": {"pin": "D13", "state": "high"}, "timeout": "1ms"}` | Attente bornée d’un niveau (`high`, `low`, `floating`) |
| `{"waitFor": {"serial": "ready", "port": 0}, "timeout": "100ms"}` | Attente bornée d’un texte émis sur un port série depuis le début de l’étape |
| `{"waitFor": {"component": "pot-a0", "state": "position", "above": 0.5}, "timeout": "1s"}` | Attente bornée d’un état de composant (`above`, `below`), sur sa valeur présente : une luminosité de LED, moyenne d’une sortie qui peut être modulée, est refusée ; on mesure alors le rapport cyclique de la broche |
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
niveau : ni son temps ni ses transitions ne comptent comme un signal), quand une fréquence ne dispose pas de deux
fronts montants, ou quand ses fronts tombent tous sur le même cycle (plusieurs stimuli appliqués au même instant) et
ne couvrent donc aucune durée. La valeur est alors absente, l’explication figure dans le rapport, et une mesure qui a un attendu
échoue.

## Ce qui est refusé avant l’essai

`siliconforge bench check` et `run` vérifient tout le scénario avant d’exécuter la moindre instruction. Chaque refus
nomme le fichier fautif, le JSON Pointer de la faute et sa cause :

| Cause | Exemples |
| --- | --- |
| `unreadable` | Scénario, circuit ou firmware illisible ; texte qui n’est pas du JSON ; HEX corrompu |
| `version` | `formatVersion` inconnu de ce moteur |
| `schema` | Étape mal formée, durée sans unité, identifiant de mesure en double |
| `wiring` | Circuit invalide, puce qui ne tient pas sur la carte, sonde sur une broche absente du breadboard |
| `resource` | Microcontrôleur inconnu, composant, état, broche ou port série absent, état non modifiable de l’extérieur |
| `capacity` | Firmware qui ne tient pas dans les mémoires du microcontrôleur, attente sur un état moyenné dans le temps (luminosité d’une LED), valeur hors des bornes d’un composant, réglage d’un générateur que l’horloge du MCU ne peut pas suivre (pas d’échantillon de moins de 16 cycles, onde arbitraire sans échantillons), sonde au-delà de l’alimentation, tolérance plus fine que la résolution de la fenêtre (100 % / nombre de cycles), fenêtre plus courte qu’une période de la plus haute fréquence attendue (elle ne peut contenir deux fronts montants pour aucune valeur attendue), fréquence au-delà de la moitié de l’horloge, durée de plus d’une heure |
| `schedule` | Étape placée (`at`) avant la durée minimale des étapes qui la précèdent |

## La commande

```text
siliconforge bench run [--out <répertoire>] [--json] <scénario.json>
siliconforge bench check <scénario.json>
```

Le banc est une commande de l’application `siliconforge` (#80) : le même exécutable ouvre la fenêtre et fait tourner
les scénarios sans fenêtre, en CI comme pour un agent. Une compilation avec `-DSILICONFORGE_BUILD_APP=OFF` fournit aussi
`build/siliconforge bench`, sans dépendre de WebFront.

**Dans le terminal de la fenêtre**, `run` confie à la fenêtre ouverte un scénario de son projet (`scenarios/*.json`,
sans `--out`). La commande lui passe l’essai par son serveur MCP ([AGENT-MCP.md](AGENT-MCP.md)), avec les variables
`SILICONFORGE_MCP_URL`, `SILICONFORGE_MCP_TOKEN` et `SILICONFORGE_PROJECT` que reçoit chaque session. La fenêtre fait
tourner l’essai sur son fil du banc et l’ouvre dans le panneau **Bench**. Elle charge ensuite le circuit et le
firmware du scénario dans l’émulation affichée et les lance : ce que l’agent vient de vérifier est ce que la personne
voit. Ce chargement n’a pas lieu si la personne garde la main (« Let the agent drive ») ou si le scénario n’a pas pu
tourner. La commande affiche le même résumé et rend le même code de sortie qu’un essai local, suivis d’une ligne
`Window: …` qui dit ce qui a été chargé. La fenêtre conserve ses preuves dans le projet. Si la commande est lancée
depuis un autre répertoire, elle y conserve également les preuves, au même chemin relatif qu'une exécution locale.

Ailleurs (CI, autre terminal), avec `--out`, pour un fichier hors de `scenarios/` du projet, pour un lien qui change le nom du scénario, ou si la fenêtre ne
répond plus, l’essai tourne sans fenêtre ; dans ce dernier cas, la commande le signale sur la sortie d’erreur. Une
connexion est abandonnée après 10 secondes et une réponse inactive après 60 secondes ; un essai déjà accepté par la
fenêtre peut alors continuer en parallèle de l'essai local.

| Code de sortie | Signification |
| --- | --- |
| 0 | Réussite : chaque attente a abouti à temps et chaque mesure respecte son attendu |
| 1 | Échec : une mesure manque son attendu, une attente atteint son échéance, ou le MCU s’arrête (opcode invalide…) |
| 2 | Banc incompatible : le scénario ne peut pas s’exécuter (causes ci-dessus) |
| 3 | Erreur d’utilisation ou d’écriture des preuves |
| 4 | Erreur interne du banc : une exception que le moteur n’a pas convertie en verdict, avec son message |

Après une attente échouée, les étapes suivantes ne sont pas exécutées (`skipped`) : elles partiraient d’un état
imprévu. Une mesure hors tolérance n’arrête pas l’essai, les suivantes sont prises.

## Les preuves d’un essai

`run` écrit dans `--out`, par défaut `.siliconforge/runs/<nom du fichier>/` dans le répertoire courant (ignoré par Git) :

- `report.json` : verdict ; version du moteur et du format ; empreinte SHA-256 et chemin du scénario, du circuit et du
  firmware ; microcontrôleur et horloge ; chaque étape avec son issue, ses instants planifiés de début et de fin, le
  cycle où un stimulus a pris effet (`applied`), en cycles et en secondes, et son détail ; chaque mesure avec sa
  valeur, son unité, sa fenêtre, le nombre de fronts montants et l’attendu ; la sortie série de chaque port, en octets
  (`base64`, réversible) et en texte (`text`, les octets qui ne forment pas de l’UTF-8 remplacés par U+FFFD) ; la
  cause d’une incompatibilité, avec l’empreinte de chaque entrée lue avant le refus.
- `trace.vcd` : toutes les broches depuis le reset, à ouvrir dans GTKWave, PulseView ou Surfer. L’échelle de temps est
  la plus grande échelle valide (1, 10 ou 100 s, ms, µs, ns, ps ou fs) qui divise la période d’horloge : 100 ps à
  16 MHz, soit 625 unités par cycle. Une période qui n’est pas un nombre entier de femtosecondes donne une échelle de
  1 fs et des instants arrondis. `report.json` indique l’échelle dans `trace.timescale`.

La trace et le rapport sont écrits même quand une assertion échoue. Les deux fichiers sont d’abord écrits en entier
(`report.json.new`, `trace.vcd.new`) puis remplacent leurs prédécesseurs par un renommage : une écriture qui échoue
laisse l’essai précédent intact. Le rapport nomme l’empreinte SHA-256 de sa trace (`trace.sha256`) et les lecteurs
la vérifient : une trace n’est rendue qu’avec le rapport qui la désigne, et les instants entre les deux renommages
apparaissent comme un désaccord (relu pendant une demi-seconde au plus), jamais comme un mélange de deux essais. Un
essai sans trace ne fait pas confiance à un `trace.vcd` resté là : il est ignoré, et supprimé par `run`. Les
fichiers temporaires `.new` d’un essai interrompu sont remplacés au suivant. Ils ne contiennent ni horodatage ni durée
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

## Dans la fenêtre

Le panneau **Bench** de la fenêtre utilise le même moteur et écrit les mêmes preuves, au même endroit, que
`siliconforge bench` : la fenêtre et la commande partagent `scenario::keepEvidence` et
`scenario::defaultEvidenceDirectory` ([`Scenario.hpp`](../include/siliconforge/scenario/Scenario.hpp)). Elle travaille
sur le répertoire courant du projet :

- la liste des scénarios vient de `scenarios/*.json`, celle des essais de `.siliconforge/runs/` (elle ne lit que les
  rapports, jamais les traces, qui peuvent être volumineuses) ;
- **Open** affiche les preuves d’un essai, quel que soit celui qui l’a lancé (l’agent, la CLI ou la fenêtre) : le
  verdict, chaque mesure avec sa valeur et son attendu, chaque étape et son issue (le stimulus appliqué figure dans le
  détail de l’étape `set`), la sortie série, l’empreinte des entrées, et la trace, que **Save trace** enregistre en
  VCD ;
- **Run** relance le scénario avec son propre MCU, sans toucher à l’émulation affichée, remplace les preuves de
  l’essai précédent du même scénario et les compare à celles-ci. Deux essais sur les mêmes entrées donnent un rapport
  identique à l’octet près ; sinon le panneau liste les chemins JSON qui diffèrent (empreinte d’une entrée, valeur
  d’une mesure, verdict).

`sf.bench.run` s’exécute sur un fil de travail (un seul, donc deux essais ne s’écrivent jamais en même temps) et sa
Promise se résout à la fin : un scénario de plusieurs minutes simulées ne retarde ni les autres appels ni les autres
connexions. Fermer la fenêtre attend la fin de l’essai en cours.

Les preuves sont lues et écrites comme des fichiers ordinaires : un lien symbolique est refusé à l’ouverture
(`O_NOFOLLOW`), une FIFO ne bloque pas la lecture, et l’écriture passe par un nouveau fichier renommé sur l’ancien,
de sorte qu’un lien physique vers un fichier extérieur n’est jamais tronqué.

La page ne donne jamais de chemin : elle nomme un scénario ou un essai de ces listes, et un nom qui contient un
séparateur est refusé (`siliconforge.unknownScenario`, `siliconforge.unknownRun`). Une interface servie à d’autres
machines (`--address 0.0.0.0`) ne peut donc ni lire ni écrire hors du projet. Les fonctions sont `sf.bench.list`,
`sf.bench.open` et `sf.bench.run` ; l’API d’automatisation les expose en `siliconForge.bench`.

## Limites

- Les stimuli disponibles sont les états interactifs des composants (boutons, potentiomètres) et les sondes de
  tension. L’alimentation pilotable, la charge variable (#62) et le générateur de signaux (#63) sont des composants
  dont on règle les états ; l’écran externe fait l’objet de la story #64.
- Les mesures portent sur une broche à la fois ; pas encore de tension moyenne, de niveau d’un nœud du circuit ni de
  décodage UART dans un scénario.
- Le premier incrément est qualifié sur l’ATmega328P simulé.
- La fenêtre affiche la trace en VCD à enregistrer, pas encore en courbes : on l’ouvre dans GTKWave, PulseView ou
  Surfer. Elle n’édite pas les scénarios et ne choisit pas d’autre répertoire que celui du projet.
