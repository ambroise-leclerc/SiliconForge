# SiliconForge

Français · [English (UK)](README.en.md)

**Émulez un microcontrôleur, observez votre circuit et testez vos firmwares sans matériel.**

SiliconForge est une application native qui exécute les binaires `.hex` et `.elf` produits par votre chaîne de
compilation. Elle réunit le circuit, le brochage, le moniteur série et les registres du processeur dans une même
fenêtre. L’émulation suit les cycles d’horloge du microcontrôleur, avec des périphériques et des entrées-sorties
simulés, pour itérer sur un firmware et vérifier son comportement de façon déterministe.

La première cible est l’**ATmega328P à 16 MHz**, le microcontrôleur de l’Arduino Uno. L’application ouvre une fenêtre
native sur macOS, Linux et Windows grâce à [WebFront](https://github.com/ambroise-leclerc/WebFront) et au moteur
système disponible. Elle peut aussi fonctionner sans affichage pour les tests et l’intégration continue.

## Observer et piloter un firmware

Capture animée de la fenêtre native sous **macOS**, avec le thème gris clair : le firmware `blink.hex` fait
clignoter la LED sur D13 pendant que le compteur de cycles et les registres évoluent.

![Fenêtre native macOS de SiliconForge : circuit sur plaque d’essai, moniteur série et registres du processeur](docs/images/siliconforge.gif)

La vue **Breadboard** relie des LED, un bouton et un potentiomètre à la puce : la LED sur D13 suit le firmware,
celle sur D6 reflète le rapport cyclique PWM, et les commandes agissent sur D2 et A0. On peut lancer, suspendre,
réinitialiser ou avancer d’une instruction, modifier la vitesse et charger un autre firmware.

La vue **Pinout** affiche les numéros physiques, les ports, les fonctions et les noms Arduino des broches, avec
leur niveau en direct, visible dans la capture animée ci-dessous. Un clic permet de forcer ou de libérer une
entrée. Les fonctions indiquées sur le brochage ne sont pas toutes émulées ; le tableau ci-dessous précise les
périphériques disponibles.

![Fenêtre native macOS de SiliconForge : brochage interactif de l’ATmega328P et état du processeur](docs/images/pinout.gif)

## État actuel

| Élément | Prise en charge |
| --- | --- |
| Cœur AVRe+ | Jeu d’instructions de l’ATmega328P et coûts en cycles ; interruptions avec priorités, réponse en 4 cycles et délai SEI/RETI ; sommeil, BREAK et auto-programmation SPM |
| Mémoires | Flash de 32 Kio, SRAM de 2 Kio, EEPROM de 1 Kio avec temporisation d’écriture |
| GPIO | Ports B, C et D : DDR/PORT/PIN, basculement par PIN, résistances de rappel et `MCUCR.PUD` |
| Timer/Counter0 | Modes normal, CTC, Fast PWM et Phase Correct PWM ; sorties OC0A/OC0B et interruptions |
| USART0 | Mode asynchrone au débit configuré, FIFO de réception à deux niveaux, débordement et interruptions |
| ADC | Conversions simples sur A0–A5 et la référence interne, avec durée de conversion |
| Descriptions de MCU | Format JSON versionné, validation du schéma et des références, chargement en C++ et description ATmega328P embarquée |
| Tests | Instructions, périphériques, chargement des descriptions et firmwares réels via le banc de test ; scénarios d’interaction Jasmine disponibles |
| À implémenter | Timer1/2, SPI, TWI, watchdog, comparateur analogique et interruptions externes ; leurs registres restent de simples registres de stockage |

### Descriptions de microcontrôleurs en JSON

Les paramètres de l’ATmega328P proviennent désormais de [`mcus/atmega328p.json`](mcus/atmega328p.json) : mémoires,
registres et valeurs de reset, vecteurs, coûts des instructions, périphériques, boîtier et brochage. La description
et son schéma sont embarqués dans le binaire. La validation combine JSON Schema Draft 7 et des vérifications de
cohérence en C++, avec des diagnostics localisés par JSON Pointer.

L’API C++ permet de charger une description depuis un fichier (`mcu::load`), du texte (`mcu::loadJson`) ou un
répertoire ajouté au catalogue (`mcu::addPath`). Les fabriques disponibles composent les GPIO, timers 8 bits,
USART, ADC et EEPROM AVR. Le JSON configure les comportements déjà implémentés ; un nouveau type de périphérique
ou une nouvelle instruction nécessite encore du C++.

Le décodeur partage désormais une table compacte de 65 536 indices sur 16 bits entre les cœurs. Chaque puce
configure ses handlers et timings à sa création, sans recherche JSON dans la boucle d’émulation. Le format,
les API et les limites sont détaillés dans [Descriptions de MCU](docs/MCU-DESCRIPTIONS.md).

### Performances mesurées

Sur `blink`, la dernière session documentée mesure une médiane de **156,4 MHz de cycles simulés**, soit environ
**9,8 fois le temps réel** pour un ATmega328P à 16 MHz (GCC 16.1, Release, cœur avec 512 Kio de cache L2).
Ce débit dépend de la machine et du firmware. Le rapport des médianes montre un recul de 4,02 % par rapport au
moteur initial dans cette session ; l’objectif de ±3 % reste à vérifier. Le [protocole et les résultats](docs/MCU-DESCRIPTIONS.md#mesures-de-la-table-compacte)
distinguent les mesures des hypothèses sur leurs causes.

## Démarrage rapide

Prérequis : **CMake 3.31+** et un compilateur avec bibliothèque standard compatible **C++26**. La CI Linux utilise
GCC 16 ; les captures macOS ci-dessus proviennent d’une compilation avec Apple Clang 21. Les dépendances
(WebFront, Catch2, nlohmann/json et le validateur JSON Schema) sont récupérées par CPM.

Sur macOS, la fenêtre native utilise WKWebView. Sous Linux, installez WebKitGTK 6.0 avant la configuration
(`sudo apt install libwebkitgtk-6.0-dev` sur Debian/Ubuntu, `webkitgtk6.0-devel` sur Fedora). Sans moteur natif
disponible à la compilation, l’application ouvre le navigateur système.

Depuis la racine du dépôt :

```bash
cmake -S . -B build                 # Release par défaut
cmake --build build -j
./build/apps/siliconforge/siliconforge firmware/prebuilt/blink.hex
```

Les [firmwares précompilés](firmware/README.md) permettent d’essayer le clignotement, l’écho série, les interruptions
Timer0 et la PWM sans installer de chaîne de compilation AVR.

Pour une exécution sans affichage, avec sortie série sur la sortie standard :

```bash
./build/apps/siliconforge/siliconforge --headless --speed max --duration 5 firmware/prebuilt/uart_echo.hex
```

`--browser` ouvre le navigateur système ; `--address 0.0.0.0 --no-browser` permet l’accès depuis une autre machine
sur le port 9002. `--mcu atmega328p` sélectionne la cible ; `--help` liste les options et les modèles du catalogue.

## Tester un firmware

Le [banc de test C++](include/siliconforge/testing/Bench.hpp) charge un binaire, avance le temps simulé et observe
les broches ou la liaison série. Exemple avec Catch2 et le firmware de démonstration, depuis la racine du dépôt :

```cpp
#include <catch2/catch_test_macros.hpp>
#include <siliconforge/mcu/Registry.hpp>
#include <siliconforge/testing/Bench.hpp>

using namespace std::chrono_literals;

TEST_CASE("le firmware répond en majuscules") {
    siliconforge::testing::Bench bench{siliconforge::mcu::create("atmega328p")};
    bench.load("firmware/prebuilt/uart_echo.hex");
    REQUIRE(bench.waitForSerial("ready\r\n", 100ms));
    bench.serial().send("hello\r");
    REQUIRE(bench.waitForSerial("HELLO\r\n", 100ms));
}
```

Pour exécuter les tests automatisés du dépôt :

```bash
ctest --test-dir build --output-on-failure
```

L’application expose aussi `window.siliconForge` pour piloter les commandes, les broches et le moniteur série dans
les scénarios d’interaction. Les [scénarios Jasmine et leur procédure d’exécution](tests/ui/README.md) sont
disponibles ; leur intégration à CTest via `wf test` reste à réaliser.

## Organisation du projet

```text
include/siliconforge/
├── core/       Interface MCU, ordonnanceur, broches, série, débogueur, chargeurs HEX/ELF
├── arch/avr/   Cœur AVR et périphériques réutilisables
├── mcu/        Descriptions, validation, catalogue et façade ATmega328P
└── testing/    Banc de test
src/            Implémentations C++
mcus/           Descriptions JSON des microcontrôleurs
schemas/        Schéma du format MCU
apps/siliconforge/   Application, simulation et liaison avec les commandes
ui/             Vues du circuit, du brochage, des broches et du processeur
firmware/       Firmwares de test et binaires précompilés
```

Le socle commun est indépendant du jeu d’instructions. Une puce réutilisant les comportements AVR existants peut
être décrite en JSON ; une autre architecture demande un cœur et ses périphériques dans `arch/`. Voir
l’[architecture](docs/ARCHITECTURE.md) et les [décisions d’architecture](docs/adr).

## Prochaines étapes

| Axe | État et référence |
| --- | --- |
| Démonstration testée de bout en bout | Circuit et commandes disponibles ; automatisation complète à poursuivre — [ADR 0003](docs/adr/0003-demo-bout-en-bout.md) |
| Descriptions de MCU | Format versionné et ATmega328P intégrés ; qualification des performances à poursuivre — [ADR 0004](docs/adr/0004-description-mcu-json.md) |
| Composants et circuits en JSON | Circuit de démonstration inclus dans la description ; modèle général de composants et de circuits à réaliser — [ADR 0005](docs/adr/0005-composants-et-circuits.md) |
| Qualification du cœur | Couverture du décodage, tests différentiels avec simavr et vérification des cycles à approfondir — [ADR 0006](docs/adr/0006-qualification-coeur.md) |

Sont également prévus : Timer1/2, interruptions externes et par changement de broche, SPI, TWI, watchdog ; écrans
simulés ; protocole GDB distant, désassemblage et pile d’appels issue des symboles ELF ; autres cibles, dont
l’ATmega2560, ARM Cortex-M0+/M4 et RISC-V.

## Licence

SiliconForge est proposé sous double licence **Apache-2.0 OR CECILL-B**, au choix. Le fichier [LICENSE](LICENSE)
contient les textes complets d’Apache 2.0 et de CeCILL-B version 1, dont les versions française et anglaise de
CeCILL-B. Les composants tiers conservent leurs propres licences.
