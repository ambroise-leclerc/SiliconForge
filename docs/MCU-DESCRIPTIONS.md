# Descriptions de MCU — format 1

`mcus/atmega328p.json` est la source des paramètres du modèle ATmega328P. Son JSON et
`schemas/mcu.schema.json` sont embarqués par CMake : une installation ne dépend pas du
répertoire source. Le chargeur utilise nlohmann/json et le validateur JSON Schema Draft 7
(pboettch), obtenus par CPM. Le schéma est strict ; les références entre registres,
vecteurs, GPIO et périphériques sont ensuite vérifiées en C++.

## Chargement C++

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
en interruption et de réveil. Les tables sont compilées en 65 536 entrées au chargement ;
aucune recherche de mnémonique ou de JSON n'a lieu dans la boucle d'exécution.

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
la couleur « serial ». `Mcu::chipJson()` expose ces métadonnées aux consommateurs du modèle.
Le champ facultatif `breadboard` conserve le circuit de démonstration existant ; une
puce sans circuit affiche quand même son brochage et ses broches interactives.

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
