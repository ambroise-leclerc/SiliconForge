# Bilan de migration vers WebFront 0.7

État au 31 octobre 2026. SiliconForge épingle la bibliothèque **v0.7.0**, commit
`7b6ccc40db24a9241c21ea44df4b5d020e56dd89`, à la place de `fd410957`.
La version de la bibliothèque ne doit pas être confondue avec celle de l'environnement développeur,
qui reste épinglé par WebFront en `developer-v0.6.3`.

> **Mise à jour du 1er novembre 2026.** Le raccordement des tests d'IHM décrit plus bas est réalisé : #8 (PR #35),
> puis #9 (PR #37), #6 (PR #38) et #7 (PR #39) sont fusionnées. Les specs tournent dans la vraie fenêtre WebKitGTK
> sous Xvfb avec `webfront_add_jasmine_test`, et le firmware est compilé depuis ses sources. Restent #10 (capture
> animée, voir [WebFront#468](https://github.com/ambroise-leclerc/WebFront/issues/468)), l'adoption de la diffusion
> (#36) et les autres chantiers. Les sections suivantes gardent l'état du 31 octobre.

## Ce qui est livré en amont

Les huit issues ouvertes dans WebFront pendant le développement de SiliconForge sont fermées avec
le motif `completed`. Les API arrivent en 0.6 ; l'isolation des options CMake arrive en 0.7.

| Issue WebFront | Apport | Conséquence pour SiliconForge |
| --- | --- | --- |
| [#396](https://github.com/ambroise-leclerc/WebFront/issues/396) | `WebFrontConfig::window` configure titre et dimensions | La fenêtre peut porter le titre SiliconForge et une taille adaptée au breadboard. Par défaut, le titre suit désormais le nom de l'exécutable. |
| [#397](https://github.com/ambroise-leclerc/WebFront/issues/397) | Détection native active chez les consommateurs, métadonnées accessibles, `webfront_use_native_window(target)` | Le réglage explicite de détection et l'inspection des définitions dans notre CMake peuvent être simplifiés ultérieurement. |
| [#398](https://github.com/ambroise-leclerc/WebFront/issues/398) | `nativeWebViewAvailable`, `NativeOrBrowserFrontend`, raison du repli | Remplace potentiellement les macros et la double construction d'application dans `main.cpp`. Préserver les modes explicites `--browser` et `--no-browser`. |
| [#399](https://github.com/ambroise-leclerc/WebFront/issues/399) | Diagnostic GCC 16 et `webfront_disable_module_scan(target...)` | Notre `CMAKE_CXX_SCAN_FOR_MODULES OFF` reste valable : le défaut du compilateur n'est pas corrigé par la mise à jour. |
| [#400](https://github.com/ambroise-leclerc/WebFront/issues/400) | `UI` assignable et comparable | Permet `vector`/`optional` ; aucun constructeur par défaut. Les valeurs restent liées à la durée de vie de l'application. |
| [#401](https://github.com/ambroise-leclerc/WebFront/issues/401) | `app.broadcast`, `app.forEachUI`, contrat de threads documenté | Diffusion d'un état commun encodé une seule fois, avec contre-pression et conservation du dernier état. Facilite les futurs composants et écrans. |
| [#402](https://github.com/ambroise-leclerc/WebFront/issues/402) | `webfront_add_jasmine_test` et `webfront::testing::JasmineTest` | Débloque les specs dans la vraie page et dans CTest, sans environnement développeur ni Node.js. |
| [#412](https://github.com/ambroise-leclerc/WebFront/issues/412) | Options de tests, fuzzing, couverture et sanitizers préfixées `WEBFRONT_` | Les options génériques du parent ne déclenchent plus les tests ou l'instrumentation de WebFront. |

Sources : [notes 0.6](https://github.com/ambroise-leclerc/WebFront/releases/tag/v0.6.0),
[notes 0.7](https://github.com/ambroise-leclerc/WebFront/releases/tag/v0.7.0),
[changelog versionné](https://github.com/ambroise-leclerc/WebFront/blob/v0.7.0/docs/CHANGELOG.md).

## Intégrations à réaliser dans SiliconForge

### Tests d'IHM : priorité à #8

Le blocage externe de #2 et #8 est levé. Le raccordement reste à implémenter :

1. Dans une construction de test, conserver un `JasmineTest` après l'enregistrement des callbacks,
   avant l'ouverture de la fenêtre ; fermer l'application puis retourner `tests.exitCode()`.
2. Enregistrer les suites avec `webfront_add_jasmine_test`, `FRONTEND WebKitGTK` sous Linux,
   des arguments de firmware et `--port 0`. Le paramètre `FRONTEND` valide le moteur mais ne
   sélectionne pas le type C++ : le programme de test doit réellement ouvrir ce moteur embarqué.
3. Ajouter le label `ui` : le helper attribue par défaut `web-integration`. Installer
   `libwebkitgtk-6.0-dev` et `xvfb` dans le job CI. Garder les rapports JSON comme artefacts.
4. Tester les scénarios UART et blink ; vérifier aussi qu'une assertion fausse fait échouer CTest.

Le helper télécharge Jasmine 7.0.2 à la configuration et vérifie son SHA-512 ; il accepte une
archive locale pour les constructions hors ligne. Il charge des **scripts JavaScript classiques**,
pas directement des modules ou du TypeScript. Il ne requiert aucun manifeste `wf`.

Cela facilite #6 et #7 (mesures sur la LED SVG), #9 (variantes de firmware compilées) et les
specs de composants #20/#22. Cela ne fournit ni les nouvelles assertions ni le modèle électrique.
À vitesse maximale, la cadence de publication peut masquer des transitions : la précision de #6
doit être vérifiée par un essai, pas déduite de l'arrivée du runner. #7 reste sensible à la charge CI.

Recette : [tests d'une application consommatrice](https://github.com/ambroise-leclerc/WebFront/blob/v0.7.0/docs/CONSUMER_TESTING.md).

### Diffusion et futurs écrans : #4, #20 et #22

`broadcast("siliconForge.onState", options)(...)` permettrait de retirer la table de clients
pour un état réellement commun ; `maxPendingWrites` retient le dernier appel pour un client lent.
Les diffusions attendent la fin des handlers de démarrage de l'UI. `forEachUI` ignore les clients
en démarrage : il ne remplace donc pas la diffusion d'états.

Notre trame contient actuellement un fragment série et un **offset par client**. Remplacer cette
trame telle quelle par un broadcast risquerait de perdre ou dupliquer des caractères : séparer
l'état commun du flux série, ou définir un protocole de séquences et de rattrapage avant migration.
Un framebuffer peut ensuite utiliser le pont binaire `Uint8Array` existant, sans base64 ; WebFront
ne fournit pas le contrôleur SSD1306/ST7735 ni le protocole SPI/I²C à émuler.

La génération de connexion ajoutée en 0.6 empêche une ancienne `UI` de viser un nouveau navigateur
réutilisant son identifiant. Utiliser les UI fournies par les callbacks ; les constructeurs publics
manuels demandent désormais aussi la génération. Conserver notre règle : aucun appel WebFront
sous un verrou pris par les callbacks.

Contrat : [exécution et threads](https://github.com/ambroise-leclerc/WebFront/blob/v0.7.0/docs/ASYNC_EXECUTION.md).

### Autres possibilités et limites

- Les documents DOM par navigateur, mises à jour groupées, événements attendables, formulaires et
  inspecteurs générés depuis des descripteurs C++ peuvent servir à de futurs éditeurs de propriétés
  de composants. Ils ne remplacent pas les schémas JSON de #18 ni le chargement de circuit de #21.
  L'adaptateur React est optionnel ; l'interface actuelle peut rester en JavaScript/SVG.
- L'agent natif et l'interface MCP permettent `check`, `build`, `test`, `preview`, `inspect`,
  `click` et `fill` dans un projet développeur préparé pour cela. Ce n'est pas une automatisation
  immédiate de toute application consommatrice. Les captures sont explicitement différées :
  l'animation CI de #10 demande toujours un outil de capture et d'encodage externe.
- Pour #34, `wf run`/`wf exec` exécutent du JS/TS dans un Worker navigateur. Ils offrent des accès
  de fichiers du projet et de réseau déclarés, mais **pas de sous-processus**, de `process` ou de
  `child_process`. `DOMParser` n'est pas à supposer disponible dans ce Worker. Un convertisseur
  ATDF en TS demande un parseur XML compatible et un mode de distribution à définir. Garder
  Python pour le convertisseur et ses tests CLI est la recommandation provisoire ; le coût CI
  comparé reste à mesurer. Tester l'IHM avec le helper CTest est une avancée indépendante.

Sources : [DOM](https://github.com/ambroise-leclerc/WebFront/blob/v0.7.0/docs/DOM.md),
[formulaires](https://github.com/ambroise-leclerc/WebFront/blob/v0.7.0/docs/FORMS.md),
[automatisation](https://github.com/ambroise-leclerc/WebFront/blob/v0.7.0/docs/AUTOMATION.md),
[scripts développeur](https://github.com/ambroise-leclerc/WebFront/blob/v0.7.0/docs/DEVELOPER_ENVIRONMENT.md#scripts-wf-run-wf-exec).

## Portée de cette migration

La modification de dépendance conserve les mécanismes applicatifs existants : repli natif manuel,
suivi des clients et lancement manuel des specs. Les issues #2, #4, #6, #7, #8, #9, #10, #20, #22
et #34 sont actualisées avec les apports et le travail restant ; leur état reste ouvert.
Les chantiers CPU (#13, #23–#27) ne sont pas débloqués par cette version.

Avec un ancien cache CMake, `ENABLE_TESTING=OFF` ne configure plus WebFront. Employer
`-DWEBFRONT_ENABLE_TESTING=OFF` pour désactiver explicitement ses propres tests ; les tests
SiliconForge restent commandés par `SILICONFORGE_BUILD_TESTS`.

## Validation locale

- Configuration du build Release existant avec `cmake -S . -B build -DWEBFRONT_ENABLE_TESTING=OFF` :
  réussie, WebKitGTK détecté, source CPM au commit de v0.7.0, sans substitution par un checkout local.
- `cmake --build build -j 2` : application, cœur et tests compilés avec succès.
- `ctest --test-dir build --output-on-failure -j 2` : **57 tests sur 57 réussis**,
  dont le pont WebBridge, les outils MCU et la CLI.
- Les tests de fenêtre native et les specs Jasmine ne font pas partie de ces 57 tests.
  Windows et macOS n'ont pas été validés dans cette session.
