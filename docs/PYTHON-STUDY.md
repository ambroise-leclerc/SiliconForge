# Sans Python ni Node.js : bilan de l'étude #34

L'issue [#34](https://github.com/ambroise-leclerc/SiliconForge/issues/34) demandait si SiliconForge pouvait se passer
de Python 3 au profit de TypeScript et des outils de WebFront. Mesures faites le 17 novembre 2026 sur WebFront 1.0.0,
Linux, GCC 16.1, build Release, WebKitGTK 2.52 sous Xvfb.

## Résultat

**Le dépôt ne contient plus de Python ni de JavaScript pour Node.js.** Chaque usage a été remplacé par ce que le projet
compile ou exécute déjà :

| Avant | Après | Où il tourne |
| --- | --- | --- |
| `tools/atdf2json` (Python) | `build/tools/atdf2json` (C++, pugixml) | Toutes les plateformes |
| `tests/McuToolsTests.py`, tests du convertisseur | `tests/unit/AtdfTests.cpp` (Catch2) et `Atdf2JsonCommandLine` (`cmake -P`) | Toutes les plateformes |
| `tests/McuToolsTests.py`, tests de la ligne de commande | `McuCommandLine` (`tests/McuCliTests.cmake`, `cmake -P`) | Toutes les plateformes |
| `tools/captures2gif` (Python, Pillow) | `build/tools/captures2gif` (C++, stb_image) et `tests/unit/GifTests.cpp` | Toutes les plateformes |
| `tests/bench/compare_dispatch.py` | `build/tests/siliconforge_compare_dispatch` (C++) et `CompareDispatchSmoke` | Linux (affinité CPU, `perf`) |
| `tests/ui/mcu.playwright.cjs` (Node.js, Playwright, manuel) | `UI-Mcu` (`tests/ui/mcu.spec.js`, Jasmine dans la fenêtre native) | Linux/WebKitGTK |

Les deux workflows n'installent plus `python3` ni `python3-pil`. Sous Ubuntu 26.04, `python3` reste toutefois présent sur
la machine, car `libwebkitgtk-6.0-dev` l'installe (`libgtk-4-dev` → `libglib2.0-dev` → `libgio-2.0-dev-bin`). Le projet
n'en dépend plus. Les commentaires de `firmware/qualification/*.c` disent seulement que leurs valeurs attendues ont été
calculées une fois avec Python ; elles sont versionnées.

Deux dépendances CPM s'ajoutent, toutes deux épinglées : pugixml 1.15 (lecture XML) et stb (décodage PNG, un seul
en-tête, commit `9600522`).

## Vérifications

| Outil | Comparaison avec l'outil Python | Résultat |
| --- | --- | --- |
| `atdf2json` | Les 133 ATDF du pack `Microchip.ATmega_DFP.3.1.264`, sans `--base` : sortie et stderr | **133/133 identiques octet pour octet** |
| `atdf2json` | ATmega328P avec `--base mcus/atmega328p.json`, quatre boîtiers dont deux erreurs | Identiques (code de sortie, JSON, message) |
| `atdf2json` | Durée des 133 conversions | 0,31 s contre 6,6 s |
| `captures2gif` | Séquence de `UI-Tour` (28 images, 7 i/s), relue par Pillow | 28 images, 140 ms chacune, boucle infinie ; 315 Kio contre 305 Kio ; écart moyen à la source 0,9 contre 2,1 par canal |
| `siliconforge_compare_dispatch` | Statistiques recalculées par les fonctions Python sur les mêmes mesures | Médianes, moyennes, écarts-types, quartiles et deltas identiques ; SHA-256 égal à `sha256sum` |

Le convertisseur garde l'ordre des clés et l'écriture des nombres (`5.0` reste `5.0`) : une réimportation avec `--base`
ne réécrit pas la description livrée. Comme l'ancien outil, il ajoute `PSRASY` aux bits de `GTCCR`, absent de
`mcus/atmega328p.json`. Cet écart est antérieur à ce changement.

L'encodeur GIF ne stocke que le rectangle modifié, rend transparents ses pixels inchangés et allonge l'image précédente
quand une image se répète. Le redimensionnement est un Lanczos-3 et la palette, unique pour toute la séquence, vient d'une
coupe médiane sur un échantillon d'images, comme dans l'ancien script. Les tests relisent le GIF avec un décodeur écrit à
partir de la spécification GIF89a.

Pour le banc, une même graine ne donne plus le même ordre de blocs ni les mêmes tirages bootstrap que le script Python.
Le générateur ne dépend désormais pas de la bibliothèque standard, et une graine donne le même ordre partout.

## Pistes écartées

### Programmes natifs depuis Jasmine (`PROGRAMS` / `webFrontHost`)

Un prototype a reproduit les tests de la ligne de commande avec
`webfront_add_jasmine_test(... PROGRAMS "$<TARGET_FILE:siliconforge>")`, `webFrontHost.withTemp`, `writeFile` et `run` :

```js
const [siliconforge] = await webFrontHost.programs();
await webFrontHost.withTemp(async (dir) => {
  await webFrontHost.writeFile(`${dir}/atmega168.json`, JSON.stringify(description));
  const result = await webFrontHost.run({program: siliconforge, args: ["--mcu-file", `${dir}/atmega168.json`], timeoutMs: 20000});
  expect(result.status).toBe("exited");
  expect(result.exitCode).toBe(1);
  expect(result.stderr).toContain(`/peripherals/${adc}/vector`);
});
```

Le prototype fonctionne et ses échecs sont lisibles : une assertion fausse remonte avec le stderr complet, et une suite
sans `PROGRAMS` échoue avec « No native capability was granted ». Mais il prend 2,8 s, dont 2,6 s de démarrage de la
fenêtre, contre 0,08 s pour `cmake -P`, et ne tourne que là où une fenêtre native est qualifiée. La fenêtre n'y sert qu'à
héberger les specs.

### Convertisseur en JavaScript

Un portage de `convert()` en JavaScript classique sous `DOMParser` donne lui aussi les mêmes sorties sur les 133 ATDF,
au formatage des flottants près (`5.0` devient `5`). Sans Node.js, il ne s'exécute cependant que dans une spec, donc dans
une fenêtre native, ou avec `wf run`, dont le Worker n'a pas de `DOMParser`. Pour un outil en ligne de commande, le C++
s'impose.

## Plateformes

| Parcours | Linux | Windows | macOS |
| --- | --- | --- | --- |
| `atdf2json`, `captures2gif` et leurs tests | Vérifié | Vérifié (MSVC 19.51, `/W4 /WX`) | Vérifié (AppleClang 21, libc++, `-Werror`) |
| `McuCommandLine`, `Atdf2JsonCommandLine` (`cmake -P`) | Vérifié | Vérifié (avec `CliHelpRejectsInvalidOptions`) | `Atdf2JsonCommandLine` vérifié ; `McuCommandLine` non exécutable (voir ci-dessous) |
| `siliconforge_compare_dispatch` | Vérifié | Non compilé (Linux seulement) | Non compilé (Linux seulement) |
| `UI-Mcu` (Jasmine) | Vérifié (WebKitGTK, Xvfb) | Non raccordé (WebView2) | Non raccordé (WKWebView) |

### macOS (AppleClang 21, arm64, macOS 27)

- Compilation de `atdf2json`, `captures2gif` et `siliconforge_tests` avec `-DSILICONFORGE_WARNINGS_AS_ERRORS=ON` :
  `<print>`, `std::ranges::next_permutation` et le formatage `chrono` sont disponibles. 92 cas de test, 91 réussis, 1 ignoré.
- `Atdf2JsonCommandLine` réussit. Comparaison avec l'outil Python historique (`git show be5b3a7:tools/atdf2json`) sur les
  133 ATDF de `Microchip.ATmega_DFP.3.1.264` : **133/133 identiques octet pour octet** (sortie et stderr), comme sous Linux.
- Écart corrigé : `cmake/Warnings.cmake` passait `-Wshadow=local`, option propre à GCC, que Clang rejette. Clang reçoit
  désormais `-Wshadow`, qui couvre aussi les champs ; trois paramètres de `Circuit.cpp` (`supply`, `source`) ont été renommés.
- Écart non corrigé : la cible `siliconforge` ne compile pas, à cause de la dépendance WebFront
  (`webfront/frontend/Capture.hpp:109`, comparaison de `std::shared_ptr<CaptureProvider>` refusée par libc++).
  `McuCommandLine` et `CliHelpRejectsInvalidOptions` ont besoin de cet exécutable : ils restent à vérifier sous macOS,
  après correction en amont. `UI-Mcu` sous WKWebView reste non raccordé.

### Windows (MSVC 19.51, Visual Studio 2026 Build Tools, CMake 4.3.4, Ninja)

- Écarts corrigés pour que `/W4 /WX` passe :
  - CMake 4.3 ne connaît pas `cxx_std_26` pour MSVC : `cmake/Cxx26.cmake` passe `/std:c++latest` à la place, et le niveau
    d'entrée ajoute `/std:c++latest /bigobj` à tout le projet. Sans cela Catch2 est compilé en C++14 et n'exporte pas
    `StringMaker<std::string_view>` (erreur d'édition de liens), et `main.cpp` dépasse la limite de sections d'un objet.
  - `_CRT_SECURE_NO_WARNINGS` pour `getenv` (C4996).
  - `json == std::string_view` est ambigu en C++20 (C2666, puis C2679 dans une assertion Catch2) : comparaison avec `std::string` à cinq endroits
    (`Description.cpp`, trois fichiers de tests).
  - `std::ranges::fill(children, 0)` sur des `uint16_t` (C4244), comparaisons signé/non signé (C4389) et `SKIP` suivi
    de code inatteignable (C4702) dans les tests.
- `siliconforge_tests` : 92 cas, 91 réussis, 1 ignoré (les 195 901 assertions passent). `Atdf2JsonCommandLine`,
  `McuCommandLine` et `CliHelpRejectsInvalidOptions` réussissent ; la cible `siliconforge` compile.
- Comparaison avec l'outil Python historique sur les 133 ATDF : 65 produisent un JSON, **identique octet pour octet sur
  la sortie standard** (CRLF des deux côtés, `std::cout` en mode texte comme Python) ; les 68 autres échouent de la même
  façon (« ATDF contains no pinout », code 1, même stderr). Avec `-o`, Python écrit en CRLF et le C++ en LF : les
  fichiers ne diffèrent que par les fins de ligne.
- Build complet en Release (type par défaut du projet) : 106 tests CTest, 0 échec, 1 ignoré (la variante compilée depuis
  `firmware/src` demande avr-gcc). `/wd4714` neutralise un avertissement d'optimiseur (`__forceinline` non inliné) sur les
  en-têtes de stdexec. En Debug, `Qualification-Cycles` dépasse son délai de 60 s (4 s en Release) : la CI compile en Release.
- Non vérifié : `UI-Mcu` sous WebView2 reste non raccordé, et les qualifications qui demandent avr-gcc ou simavr.
