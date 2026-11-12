# Migration vers WebFront 0.9

SiliconForge épingle le commit `e2d93348abd73c24cb26338843c572ce210d49f1` (v0.9.0, dernière révision mineure avant la
1.0) ; `CPM_WebFront_SOURCE` permet toujours une surcharge locale. Ce document succède au [bilan 0.8](WEBFRONT-0.8.md),
qui devient historique (le [bilan 0.7](WEBFRONT-0.7.md) l'est déjà). Suivi : [#41](https://github.com/ambroise-leclerc/SiliconForge/issues/41).

## Dépendance

- Bibliothèque 0.9.0, environnement développeur 0.9.0 servi depuis <https://dist.webfront.fr/> (compatibilité :
  bibliothèque 0.9.0, environnement 0.9.0, api 1). La mise en garde de la 0.8 sur le bootstrap développeur, le pin 0.6.3 et
  le provisionnement explicite de la 0.6.4 ne sont plus d'actualité. SiliconForge n'appelle toujours pas
  `bootstrap("developer")` : son application et ses tests Jasmine n'ont besoin ni de l'environnement développeur, ni de
  Node.js, ni de Python.
- GCC 16.1.0 avec notre `-Werror` : la correction de `fs::detail::pageOf` (WebFront#517) fait compiler la bibliothèque sans
  avertissement. `CMAKE_CXX_SCAN_FOR_MODULES OFF` reste nécessaire.
- #36 : `tests/CMakeLists.txt` continue de lire `INTERFACE_COMPILE_DEFINITIONS` de `WebFront::WebFront` pour décider de
  la construction des tests natifs, car `embeddedFrontendAvailable` est une constante C++ qui ne peut pas piloter CMake.
  `apps/siliconforge/main.cpp` n'utilise que la constante C++.

## Validation

Commandes : `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build`, `ctest`, sous GCC 16.1.0 / Linux.

| Configuration | Résultat |
| --- | --- |
| WebKitGTK (fenêtre native, Xvfb pour les specs) | 68 tests : 63 rapides et 5 suites Jasmine (`UI-Uart`, `UI-Blink`, `UI-Led`, `UI-LedRealtime`, `UI-Files`) |
| Sans moteur embarqué (`-DWEBFRONT_EMBED_WEBKITGTK=OFF -DWEBFRONT_AUTODETECT_NATIVE=OFF`) | 63 tests rapides, aucune suite Jasmine (elles exigent WebKitGTK) ; l’application se replie sur le navigateur système |
| CEF seul | **Non vérifié** : CEF n'est pas installé sur la machine de validation. La sélection de fenêtre le prévoit (`embeddedFrontendAvailable`), mais aucune suite ne l'a exercé. |
| Windows (WebView2), macOS (WKWebView) | **Non vérifiés** ; leurs injections de test (sélecteur, boîtes de dialogue) sont propres à chaque moteur. |

## Adopté

- **Chaînes longues (WebFront#456).** Le découpage du flux série en tranches de 16 Kio « parce que les chaînes sont
  limitées à 64 Kio » est supprimé. Il reste un plafond par trame (`WebBridge::maxSerialPerFrame`, 64 Kio d'octets série)
  qui évite qu'une rafale retarde les broches. Couvert par `UI-Files` : une description de plus de 64 Kio entre par
  `sf.loadMcu` et une puce de plus de 64 Kio revient par `sf.chip`, dans la fenêtre native, ainsi qu'une trame d'état
  de plus de 64 Kio. Le nom de test contient des espaces : WebKit met plusieurs secondes à mettre en page un « mot » de
  100 Kio, ce qui mesurerait la page et non le pont.
- **Listes de chaînes (WebFront#457).** `sf.mcus` renvoie la liste ; le `JSON.parse` de `ui/siliconforge.js` disparaît.
- **Erreurs typées (WebFront#458).** `sf.selectMcu`, `sf.loadMcu` et `sf.loadFirmware` ne renvoient plus « une chaîne
  d'erreur, vide en cas de succès » : elles rejettent avec une `webfront::BridgeError` dont le code est l'un des
  `siliconforge.unknownMcu`, `.invalidMcu` (le message commence par le chemin JSON), `.invalidFirmware` (le message se
  termine par `(line N)` quand la ligne est connue) et `.firmwareRejected`. Côté page, `error.code` est disponible.
  Cela prépare #21 (un circuit invalide signalé avec son chemin JSON) et #18. Le chemin et la ligne restent dans le
  message : `BridgeError` ne porte qu'un code et un texte.
- **Boîtes de dialogue.** L'échec de chargement d'un firmware s'affiche dans la page (`#firmware-error`) au lieu de
  `alert()`, qui ne faisait rien sous WKWebView avant la 0.9.
- **Chargement par le sélecteur de fichiers.** `UI-Files` charge un firmware et une description de MCU par les
  `<input type="file">` de la page, vérifie les messages d'échec et l'absence d'`alert()`. **Limite :** la boîte de
  dialogue système n'est pas pilotée. Les points d'injection de WebFront (`Session::chooseFiles` et
  équivalents) sont internes à chaque moteur et sans API consommateur ; la spec donne un `File` à l'input par
  `DataTransfer` et émet `change`, comme le fait le moteur une fois le fichier choisi.

## Évalué et non retenu pour l'instant

| Sujet | Décision | Raison |
| --- | --- | --- |
| Rappels asynchrones (`ResultTask`) | Écarté | Chargement d'une description de 100 Kio mesuré à 4–8 ms (aller-retour compris) ; aucun traitement long ne tient le verrou de simulation. À reconsidérer avec un import lourd (#21). |
| Tableaux typés et bitsets pour l'état poussé | Reporté à #20 | L'état poussé reste un fragment JSON par trame, sans goulot mesuré ; le gain éventuel ne se juge qu'avec les captures de diagnostic de #20. |
| Export du journal série ou d'un circuit (téléchargement) | Écarté | Non demandé aujourd'hui ; WebKitGTK enregistre sans dialogue dans le dossier de téléchargement, ce qui demande une décision d'interface. À reprendre avec la persistance des circuits (#21). |
| `enableAutomation` / `webfront-agent` | Écarté | Exige `bootstrap("developer")` (environnement développeur téléchargé depuis `dist.webfront.fr` au démarrage), que l'application n'utilise pas ; les specs Jasmine couvrent déjà les parcours automatisés sans cela. Les captures ne sont pas exposées par Automation/MCP. `mcu.playwright.cjs` reste le scénario manuel. |
| `UI::isEmbeddedView()` / `isLocalClient()` | Écarté | Le navigateur système n'est jamais « local » sans `trustLoopbackClients`, donc réserver `sf.loadFirmware`, `sf.loadMcu` et `sf.selectMcu` aux clients locaux casserait `--browser`. La protection utile (écoute sur une adresse non bouclée) relève plutôt d'un refus explicite de `--address` ; à traiter dans une issue dédiée si le besoin se précise. |
| `fs::Storage`, routes dynamiques, lectures liées au client, `UIExecutor`, `NativeWindow`, `@webfront/services` | Non retenu | Aucun besoin actuel. À reconsidérer pour un sélecteur de fichiers natif, la persistance de circuits (#21) ou l'export de traces volumineuses. |

## Reporté de la 0.8

Ces chantiers ne dépendent pas de la 0.9 et restent à réaliser dans leurs issues : [#34](https://github.com/ambroise-leclerc/SiliconForge/issues/34)
(programmes natifs `PROGRAMS` / `webFrontHost` et coût du remplacement de Python), [#10](https://github.com/ambroise-leclerc/SiliconForge/issues/10)
(images par `webFrontCapture`, encodage GIF/WebM en CI) et [#20](https://github.com/ambroise-leclerc/SiliconForge/issues/20) /
[#22](https://github.com/ambroise-leclerc/SiliconForge/issues/22) (captures de diagnostic de rendu).

Rappels : le helper Jasmine consomme du JavaScript classique ; les scripts `wf run` / `wf exec` n'ont pas d'API de processus ;
les captures contiennent le contenu web sans décorations et leur encodage reste externe.
