# Descriptions de MCU — format 1

`mcus/atmega328p.json` est la source des paramètres du modèle ATmega328P. Son JSON et
`schemas/mcu.schema.json` sont embarqués par CMake : une installation ne dépend pas du
répertoire source. Le chargeur utilise nlohmann/json et le validateur JSON Schema Draft 7
(pboettch), obtenus par CPM. Le schéma est strict ; les références entre registres,
vecteurs, GPIO et périphériques sont ensuite vérifiées en C++.

## Charger une puce

```sh
siliconforge --mcu-file ma-puce.json firmware.hex
siliconforge --mcu-path ./mes-puces --help
siliconforge --mcu-path ./mes-puces --mcu atmega168 firmware.hex
```

`--mcu-path` peut être répété. Les fichiers `.json` sont lus dans l'ordre alphabétique.
Les descriptions invalides et les noms en double sont signalés ; le catalogue conserve
le contenu validé au démarrage. `--help` découvre les puces quel que soit l'ordre des
options. Les noms du catalogue sont insensibles à la casse.

Dans l'interface, choisir une puce dans la liste, utiliser **Load MCU…**, ou déposer un
fichier JSON. Le remplacement met la simulation en pause et efface le firmware ; charger
ensuite le firmware de la nouvelle puce. Une erreur laisse la puce courante intacte et
s'affiche avec son JSON Pointer, par exemple `/peripherals/6/vector`.

En C++ :

```cpp
#include <siliconforge/mcu/Registry.hpp>
auto result = siliconforge::mcu::load("ma-puce.json");
if (!result) {
    // result.error().path : JSON Pointer ; result.error().message : diagnostic
} else {
    auto mcu = std::move(*result);
}
```

`mcu::loadJson(text)` accepte le contenu d'un fichier ; `mcu::validate(json)` valide
sans construire de puce. `mcu::create("atmega328p")` et la façade C++ `Atmega328p`
utilisent la description intégrée, sans dupliquer ses paramètres dans du code C++.

## Champs et unités

Toutes les adresses sont des **adresses d'octets**, dans l'espace indiqué. Les valeurs
numériques sont décimales (JSON ne permet pas `0x…`). Les cycles sont des cycles du CPU,
sauf `conversionCycles` et `firstConversionCycles`, exprimés en cycles d'horloge ADC.
Les champs se lisent ainsi (extraits annotés en JavaScript, retirer les commentaires
pour obtenir du JSON) :

```js
{
  "formatVersion": 1,              // version de l'interface de données
  "name": "ATmega328P",
  "architecture": "avr",
  "core": "avr:AVRe+",             // variante ; les timings sont explicites
  "clockHz": 16000000,
  "supplyVoltage": 5.0,
  "memories": {
    "flash": {"start": 0, "size": 32768, "pageSize": 128},
    "sram": {"start": 256, "size": 2048},
    "eeprom": {"start": 0, "size": 1024, "pageSize": 4},
    "registers": {"start": 0, "size": 32},
    "io": {"start": 32, "size": 224}
  },
  "timing": {
    "nop": {"cycles": 1},
    "brbc": {"cycles": 1, "taken": 2},
    "cpse": {"cycles": 1, "skipOneWord": 2, "skipTwoWords": 3},
    "lds": {"cycles": 2, "sram": 2}, // SRAM : adresse au-delà de la zone I/O
    "call": {"cycles": 4, "pc3Bytes": 5}
    // Toutes les instructions décodées doivent être présentes, formes LD/ST comprises.
  },
  "registers": {
    "ADCSRA": {"address": 122, "reset": 0, "bits": {"ADEN": 128, "ADSC": 64}}
    // bits = masques, y compris champs multibits ; registres de 16 bits divisés en L/H.
  },
  "vectors": {"RESET": 0, "ADC": 21}, // numéros de slots, non adresses d'octets
  "peripherals": [
    {
      "type": "avr.adc", "name": "ADC",
      "registers": {"control": "ADCSRA", "mux": "ADMUX", "low": "ADCL", "high": "ADCH"},
      "vector": "ADC",
      "prescalers": [2, 2, 4, 8, 16, 32, 64, 128],
      "conversionCycles": 13,
      "firstConversionCycles": 25,
      "modelFirstConversion": false, // compatibilité historique ; true active les 25 cycles
      "channels": ["PC0", "PC1", "PC2", "PC3", "PC4", "PC5"],
      "bandgapChannel": 14, "bandgapVoltage": 1.1
    }
  ]
}
```

Le fichier complet valide est `mcus/atmega328p.json`, à utiliser comme point de départ.
`cpu` contient les registres du cœur, `pcBytes` (2 ou 3), `vectorWords`, les cycles d'entrée
en interruption et de réveil. Les 65 536 opcodes sont décodés une fois par processus en indices `u16` ;
chaque puce configure ensuite son petit tableau de handlers et de timings. Aucune recherche
de mnémonique ou de JSON n'a lieu dans la boucle d'exécution.

Les fabriques AVR enregistrées sont :

| Type | Paramètres principaux |
| --- | --- |
| `avr.gpio` | PIN/DDR/PORT, MCUCR, broches dans l'ordre des bits |
| `avr.timer8` | registres, trois vecteurs, huit prescalers, deux sorties GPIO |
| `avr.usart` | registres, trois vecteurs, GPIO RX/TX, diviseurs normal/U2X, profondeur FIFO |
| `avr.adc` | registres, vecteur, canaux, prescalers, durées de conversion |
| `avr.eeprom` | registres, vecteur, `writeTimeNs`, `readCycles` |

Un type inconnu est refusé explicitement. Les registres des fonctions qui ne sont pas
encore émulées restent des registres de stockage et ne sont pas déclarés comme instances
de périphériques. La sémantique des bits AVR reste en C++ ; leurs noms/masques documentent
le registre. Les adresses et valeurs de reset viennent du JSON.

`package` décrit le boîtier (numérotation consécutive et paire), les broches physiques,
`port` (ou `null` pour l'alimentation), les `functions`, l'étiquette de carte `label` et
les `labels` de présentation avec leurs `slotKinds`. Une étiquette `TXD:serial` choisit
la couleur « serial ». `sf.chip()` sert ces données aux vues Pinout et Breadboard.
Le champ facultatif `breadboard` conserve le circuit de démonstration existant ; une
puce sans circuit affiche quand même son brochage et ses broches interactives.

## Ajouter une puce AVR depuis un ATDF

Le convertisseur Python 3 n'a aucune dépendance externe :

```sh
tools/atdf2json ATmega168.atdf --package PDIP28 -o squelette.json
# Compléter les champs manquants puis valider en chargeant la description.
```

Sans `--base`, les champs absents de l'ATDF sont laissés vides ou à `null` : le résultat
est un **squelette à compléter**, pas une puce supposée fonctionnelle. L'ATDF fournit
les mémoires, registres, masques de bits, vecteurs et fonctions/brochage ; il ne fournit
pas la table de cycles, les connexions aux fabriques d'émulation, l'horloge choisie,
les étiquettes Arduino, le circuit ni toujours les resets.

Pour conserver ces compléments manuels en réimportant les données du constructeur :

```sh
tools/atdf2json ATmega328P.atdf --package PDIP28 \
  --base mcus/atmega328p.json -o reimporte.json
```

La description livrée a été vérifiée avec `atdf/ATmega328P.atdf` du pack
`Microchip.ATmega_DFP.3.1.264.atpack`, disponible sur
[le dépôt officiel des packs Microchip](https://packs.download.microchip.com/).
Cette commande reproduit son contenu. Les références d'instances sont résolues vers
les groupes de registres du module, les offsets d'instance sont ajoutés, et les registres
multioctets sont divisés en octets (`UBRR0L/H`, `ADCL/H`, `SPL/H`). Les resets absents de
l'ATDF sont conservés depuis la base. Les numéros physiques et fonctions sont toujours
réimportés ; les couleurs et étiquettes de carte sont conservées pour la même broche.

Pour dériver un ATmega168 du modèle livré : changer `name`, réduire les tailles flash,
SRAM et EEPROM, et adapter `SPH.reset` au nouveau sommet de SRAM (4 pour 1 Kio).
Les tests exécutent `blink.hex` sur cette variante sans nouveau C++.

## Limites du modèle

Le schéma décrit les paramètres des sémantiques actuellement implémentées : les noms
AVRe/AVRxm/AVRxt sélectionnent une table de timings, sans ajouter les instructions
spécifiques de ces variantes. Le cœur possède encore les pointeurs de données AVR
16 bits et une flash de 128 Kio maximum. Changer le JSON ne suffit pas à émuler un
nouveau type de périphérique ou une instruction absente du décodeur.

## Contrats et compatibilité

`cpu.spmcsr` et `cpu.smcr` sont facultatifs. Les omettre désactive respectivement
l'auto-programmation SPM et le contrôleur de sommeil (adresse sentinelle zéro du cœur).
Un champ présent doit toujours référencer un registre déclaré ; `null` et les références
inconnues ne sont pas acceptés.

Deux différences intentionnelles avec l'ancien modèle sont couvertes par des tests :
effacer ADEN annule une conversion ADC en cours, efface ADSC et rétablit le délai de
première conversion au prochain démarrage ; un segment de firmware dépassant la mémoire
de la puce provoque `std::out_of_range`, avant toute modification du firmware courant,
au lieu d'être tronqué ou replié par le masque de flash.

Les constantes publiques historiques de `Atmega328p` sont conservées pour compatibilité
avec les consommateurs C++ ; un test vérifie leur accord avec le JSON embarqué. Elles
ne paramètrent pas le modèle. `mcu::addPath` s'utilise uniquement à l'initialisation,
avant tout accès concurrent au catalogue.

Le chargeur AVR accepte uniquement les segments `Program` et `Eeprom`. Un segment
`Data`, même situé dans la SRAM valide, est rejeté par `std::invalid_argument` avant
modification du firmware courant : il ne sert pas à préinitialiser la SRAM. Les images
ELF doivent donc porter leurs données d'initialisation dans la flash (adresse physique),
comme les exécutables AVR usuels dont le code de démarrage initialise la SRAM.

Les registres du cœur déclarés dans `cpu`, y compris les contrôles facultatifs SPMCSR
et SMCR, sont réservés : un périphérique ne peut pas les remapper. L'indice du bandgap
ADC doit être supérieur ou égal au nombre de canaux externes afin de rester accessible.

Les consommateurs C++ de `builtinDescription()` ou du contenu JSON doivent inclure
`<nlohmann/json.hpp>` explicitement. Les en-têtes publics MCU ne fournissent que les
déclarations anticipées JSON ; un test de compilation vérifie cette indépendance.
Un `AvrCore` construit directement doit recevoir `setTiming()` avant `run()` ou `step()`
(sinon `std::logic_error`). `Device` effectue ce chargement lors de sa construction.

`available()` retourne les identifiants de catalogue en minuscules, utilisés par les
options CLI et la liste de sélection de l'IHM. `Mcu::name()` conserve la casse du JSON
et sert au nom affiché dans l'en-tête de l'IHM. Cette distinction est testée.

La table de 65 536 indices `u16` occupe 128 Kio, partagés par tous les cœurs. Le petit
catalogue partagé distingue 87 handlers (notamment LPM implicite et explicite, de même
mnémonique). Chaque puce conserve 87 paires handler/timing, soit 1 392 octets sur la
plateforme 64 bits mesurée. Les accès JSON sont calculés une fois par mnémonique lors
de `setTiming()`, puis réutilisés pour les handlers partageant ce mnémonique.
L’ancienne API de diagnostic `instructionTable()` est conservée ; sa table de pointeurs
de 512 Kio n’est initialisée que si un consommateur la demande et n’est pas utilisée
par la boucle d’émulation. Les
mesures de vitesse restent propres au firmware et au cache de la machine ; elles ne
garantissent pas l'absence de régression pour tous les ensembles d'instructions actifs.

Mesure historique de l’ancienne table privée de 1 Mio : sur un cœur avec 512 Kio de L2, en Release GCC 16.1, affinité CPU fixe,
huit exécutions dans l'ordre avant/après/après/avant répété, 30 secondes simulées de
`blink` par exécution : médianes de 164,75 MHz avant et 154,8 MHz après, soit **−6,0 %**.
Le temps CPU (démarrage compris) donne également environ −5,9 %. Ce résultat dépasse
l'objectif de ±3 % de l'ADR ; la mesure initiale de +0,2 % ne permet donc pas de conclure
à une absence générale de régression. L'écart n'est pas attribué au seul cache L2.
Les échantillons et les empreintes des exécutables sont conservés dans
[le relevé du benchmark](benchmarks/mcu-json-l2-512k.json).

## Mesures de la table compacte

La comparaison alternée entre le moteur initial, l'ancienne table privée de 1 Mio et
la table compacte (même CPU, L2 de 512 Kio, GCC 16.1 Release, `blink`, 30 s simulées)
donne des médianes de 165,7 / 159,85 / 156,85 MHz. La table compacte économise la mémoire
et le temps de construction mais n'atteint pas encore l'objectif d'exécution de ±3 % :
l'écart mesuré face au moteur initial reste **−5,34 %**. L'indirection supplémentaire
n'est donc pas présentée comme une correction démontrée du débit. Les valeurs demeurent
sensibles au firmware et à la machine.

Sur trois séries de 100 opérations après échauffement, la création d'une puce passe
de 5 538 à 463 µs et `setTiming()` de 4 833 à 21 µs (environ ×12 et ×231).
Le benchmark manuel est disponible sans alourdir la construction normale :

```sh
cmake --build build --target siliconforge_mcu_bench
build/tests/siliconforge_mcu_bench
```

Les échantillons sont dans [le relevé de comparaison](benchmarks/mcu-compact.json).

## Limite des timings avec PC sur trois octets

`pc3Bytes` remplace uniquement `cycles` lorsque `cpu.pcBytes` vaut 3. Les champs
`taken`, `skipOneWord`, `skipTwoWords` et `sram`, lorsqu'ils sont présents, conservent
leur valeur JSON ; ils ne possèdent pas de variante spécifique au PC sur trois octets.
Les cas absents héritent du coût de base sélectionné. Un futur MCU nécessitant des
variantes de ces cas demandera une extension versionnée du format.
