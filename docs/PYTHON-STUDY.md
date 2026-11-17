# Python, TypeScript et WebFront : bilan de l'étude #34

L'issue [#34](https://github.com/ambroise-leclerc/SiliconForge/issues/34) demandait si SiliconForge pouvait se passer
de Python 3 au profit de TypeScript et des outils de WebFront. Mesures faites le 17 novembre 2026 sur WebFront 1.0.0,
Linux, GCC 16.1, build Release, WebKitGTK 2.52 sous Xvfb.

## Recommandation

**Ne pas retirer Python ; le rendre facultatif.** Le convertisseur ATDF (`tools/atdf2json`) et ses tests restent en
Python, comme deux outils annexes hors CTest : `tools/captures2gif` (animation du README, Pillow) et
`tests/bench/compare_dispatch.py` (mesures de performance). La compilation et les autres tests ne dépendent plus de
Python ni de Node.js :

- Les tests de la ligne de commande passent dans un script CMake, `tests/McuCliTests.cmake` (test `McuCommandLine`),
  qui tourne sur toutes les plateformes, sans fenêtre.
- Le scénario manuel Playwright (`tests/ui/mcu.playwright.cjs`) devient la suite Jasmine native `UI-Mcu`, lancée par
  CTest et la CI.
- `find_package(Python3)` n'est plus `REQUIRED` : sans Python, la configuration l'indique et seul `McuDescriptionTools`
  (les tests du convertisseur) est omis.

La suppression complète n'est pas favorable : aucune issue de mise en œuvre n'est donc ouverte. Les raisons suivent.

## Chiffres

| Mesure | Python | WebFront | Commentaire |
| --- | --- | --- | --- |
| Tests CLI (`--help`, chargement par fichier et par nom, description invalide) | 0,07 s | 2,8 s (`PROGRAMS`, 3 specs) | Script CMake retenu : **0,08 s**, toutes plateformes |
| Coût fixe d'une suite Jasmine (une spec vide) | — | 2,6 s | Démarrage de la fenêtre, de Jasmine et du pont |
| Tests du convertisseur (8 cas) | 0,06 s | non porté | — |
| Conversion des 133 ATDF du pack `Microchip.ATmega_DFP.3.1.264` | 6,6 s (un processus par fichier) | identique | Portage JS : **133/133 sorties octet pour octet identiques** ; avec `--base`, JSON identique mais `5.0` devient `5` |
| `UI-Mcu` (ancien scénario Playwright, 9 specs) | — | 4,6 s | Stable sur trois exécutions |
| Paquets CI propres à Python (Ubuntu 26.04) | ≈ 24 Mo installés | — | **Déjà tirés par WebKitGTK** (voir ci-dessous) |

### Le coût CI de Python est nul sous Linux

Sous Ubuntu 26.04, `libwebkitgtk-6.0-dev` dépend de `libgtk-4-dev` → `libglib2.0-dev` → `libgio-2.0-dev-bin`, qui dépend
de `python3` (outils `gdbus-codegen`, `glib-mkenums`). La configuration qui exécute les suites Jasmine installe donc
Python, qu'on le demande ou non. Retirer `python3` de la liste `apt` du workflow n'économise rien. `python3-pil` reste
nécessaire à `tools/captures2gif` (animation du README, [#10](https://github.com/ambroise-leclerc/SiliconForge/issues/10)).

## Ce qui a été essayé

### Tests CLI par `PROGRAMS` / `webFrontHost`

Un prototype `UI-McuTools` a reproduit les tests CLI de `McuToolsTests.py` avec
`webfront_add_jasmine_test(... PROGRAMS "$<TARGET_FILE:siliconforge>")`, `webFrontHost.withTemp`, `writeFile` et `run`
(environnement vide, `timeoutMs: 20000`) :

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

Résultat : fonctionnel et bien diagnostiqué. Une assertion fausse remonte dans le rapport CTest avec le stderr complet
du programme ; une suite sans `PROGRAMS` échoue avec « No native capability was granted ». Mais il coûte 40 fois le
temps du script Python, ne tourne que là où une fenêtre native est qualifiée (Linux/WebKitGTK ou CEF aujourd'hui) et
n'apporte rien que `cmake -P` ne fasse déjà : la fenêtre ne sert qu'à héberger les specs. Il n'a pas été conservé.

### Convertisseur ATDF en JavaScript

Le portage de `convert()` en JavaScript classique (≈ 130 lignes, `DOMParser` de la page Jasmine) a été comparé à l'outil
Python sur les 133 ATDF du pack ATmega, l'outil Python étant lancé depuis la spec par `PROGRAMS`. Toutes les sorties sont
identiques. Deux points de portage demandent de l'attention : `int(x, 0)` (préfixes `0x`, `0o`, `0b`, rejet de `010`) et
le formatage des flottants (`5.0` en Python, `5` en JavaScript), qui modifierait `mcus/atmega328p.json` à la
réimportation.

Le convertisseur est pourtant un **outil en ligne de commande pour le développeur** (`tools/atdf2json fichier.atdf -o
squelette.json`). Sans Python ni Node.js, WebFront ne peut l'exécuter :

- dans une spec Jasmine, ce qui suppose une fenêtre native et une entrée CTest pour chaque conversion ;
- avec `wf run` / `wf exec`, dans un Worker sans `DOMParser` : il faudrait un parseur XML JavaScript installé et
  verrouillé par `wf install`, plus l'environnement développeur (`bootstrap("developer")`) et une compilation TS.

Les deux voies remplacent un script de 150 lignes sans dépendance par une chaîne plus lourde, pour un outil qui sert à
l'ajout d'une puce. Maintenir deux convertisseurs en parallèle doublerait la maintenance.

### Scénario MCU Playwright

Réécrit en `tests/ui/mcu.spec.js` (`UI-Mcu`) : sélection du catalogue, fichier modifié portant un nom du catalogue,
absence d'écriture répétée du sélecteur, identifiants serveur exacts, ATmega168 choisi par `<input type="file">`, firmware
exécuté sur la puce chargée, chemin JSON d'une description invalide, nouvelle tentative après un échec transitoire de
`chip()`, étiquettes génériques d'une puce sans circuit, retour au catalogue. Le scénario n'avait pas été relancé depuis
WebFront 0.9 ; seule l'attente de la liste des broches, remplie par une trame postérieure au changement de puce, a dû
être ajoutée. Comme `UI-Files`, la boîte de dialogue système n'est pas pilotée.

## Plateformes

| Parcours | Linux | Windows | macOS |
| --- | --- | --- | --- |
| `McuCommandLine` (`cmake -P`) | Vérifié | Attendu (CMake seul), non vérifié | Attendu, non vérifié |
| `McuDescriptionTools` (Python, facultatif) | Vérifié | Si Python est installé | Si Python est installé |
| `UI-Mcu` (Jasmine) | Vérifié (WebKitGTK, Xvfb) | Non raccordé (WebView2) | Non raccordé (WKWebView) |
| `PROGRAMS` / `webFrontHost` | Vérifié (prototype) | Non qualifié en amont | Non qualifié en amont |

## Ce qui ferait réviser la recommandation

- Un exécutable WebFront capable de lancer un module TS hors fenêtre avec un parseur XML : le convertisseur pourrait alors
  devenir un outil TS distribué.
- Une CI Windows ou macOS sans Python : seul `McuDescriptionTools` y manquerait, et le convertisseur pourrait être testé
  par une spec Jasmine comparant sa sortie à des fixtures figées.
