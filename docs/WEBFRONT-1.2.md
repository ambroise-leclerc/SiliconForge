# Passage à WebFront 1.2

SiliconForge épingle le tag [`v1.2.0`](https://github.com/ambroise-leclerc/WebFront/releases/tag/v1.2.0) de WebFront
(commit `215758021c62a76b632c7d8c70409231990dba9e`, publié le 24 novembre 2026). `CPM_WebFront_SOURCE` permet toujours
une surcharge locale. Ce document succède au [bilan 1.0](WEBFRONT-1.0.md), qui devient historique.

## Ce qui change pour nous depuis la 1.1.1

| Changement de WebFront | Effet sur SiliconForge |
| --- | --- |
| `WebView2Options` devient `ViewOptions` (WebFront#652) | Sans effet : l'application ne configure pas WebView2 elle-même. |
| `webfront-agent`, `webfront_add_agent()` et `WEBFRONT_BUILD_AUTOMATION` retirés (WebFront#632) | Sans effet : l'agent utilise le serveur MCP de SiliconForge ([AGENT-MCP.md](AGENT-MCP.md)), pas celui de WebFront. |
| `enableAutomation` n'est plus compilé avec `WEBFRONT_DISTRIBUTION` | Sans effet : nous ne l'appelons pas. |
| Profil CEF dans `cef_cache/<exécutable>` (WebFront#617) | Sans effet : CEF n'est pas le moteur choisi par défaut et l'application ne conserve rien dans son profil. |
| `webfront::bdd` public (WebFront#647) | Sans effet : nos tests gardent leur propre Catch2. |

La compilation avec `-Werror` passe sans modification du code existant.

## Adopté dans cette version

- **Repli sur un port libre (WebFront#617).** `--port` n'impose plus `9002`. Sans `--port` ni `WEBFRONT_PORT`, l'application
  prend `9002` et, s'il est occupé, un port libre : deux fenêtres, ou une fenêtre et une exécution de la CI, ne se
  gênent plus. Un port choisi n'est jamais remplacé : avec `--port 9100` ou `WEBFRONT_PORT=9100`, un port occupé fait
  échouer le démarrage en nommant l'adresse et la cause (vérifié). Pour lancer plusieurs instances avec `WEBFRONT_PORT`
  défini, utiliser `WEBFRONT_PORT=0` (port libre) ou un port différent par instance. L'adresse affichée au démarrage,
  celle du serveur MCP de l'agent et les sessions du terminal portent le port réellement choisi. Pour Mistral Vibe,
  dont la configuration est statique, il faut toujours fixer `--port 9002`.

## Évalué et non retenu

| Sujet | Décision | Raison |
| --- | --- | --- |
| `enableAutomation` : le serveur MCP de WebFront (WebFront#632, #637) | Non retenu | Il pilote le DOM de la fenêtre ; notre serveur expose les actions de l'émulation (firmware, composants, mesures, bench) et son journal s'affiche dans l'interface. Le jeton et le port se fixent déjà par `SILICONFORGE_MCP_TOKEN` et `--port`. |
| Seconde fenêtre native (`openSecondaryWindow`, WebFront#616) | Reporté | Candidat pour détacher les instruments sur un second écran. Il faut d'abord découper l'interface en deux pages qui partagent la trame d'état ; aucune issue ne le demande. |
| HTTPS et WSS (WebFront#631) | Non retenu | L'interface n'écoute que sur la boucle locale par défaut. |

## Validation

Commandes : `cmake -S . -B build -DSILICONFORGE_WARNINGS_AS_ERRORS=ON`, `cmake --build build`, `ctest`, sous GCC 16
et Linux (WebKitGTK).

- Compilation et suites rapides : passent.
- Suites Jasmine : `UI-Agent` a échoué une fois lors d'un `ctest -j4` complet et passe à la relance isolée (8,5 s) ;
  à surveiller si elle se reproduit.
- Port 9002 occupé : l'application démarre sur un port libre ; avec `--port 9002` ou `WEBFRONT_PORT` sur un port occupé, elle refuse en nommant la cause.
- Windows (WebView2), macOS (WKWebView) : non vérifiés.
