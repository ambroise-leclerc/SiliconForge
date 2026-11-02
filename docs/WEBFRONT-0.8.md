# Migration vers WebFront 0.8

SiliconForge épingle le commit `15f54631715309fb2848b72344c6829d3206d3a9` ; `CPM_WebFront_SOURCE` permet toujours une surcharge locale. Le [bilan 0.7](WEBFRONT-0.7.md) reste un document historique.

La sélection de fenêtre utilise `embeddedFrontendAvailable` et `EmbeddedWebFront` : moteur natif prioritaire, CEF si disponible seul, puis navigateur si aucun moteur n'est compilé ou si l'ouverture échoue. Les décisions CMake des tests restent fondées sur les définitions du moteur ; le booléen C++ ne les remplace pas.

Les chantiers [#34](https://github.com/ambroise-leclerc/SiliconForge/issues/34) (programmes natifs et coût du remplacement de Python), [#10](https://github.com/ambroise-leclerc/SiliconForge/issues/10) (captures et encodage), [#20](https://github.com/ambroise-leclerc/SiliconForge/issues/20) et [#22](https://github.com/ambroise-leclerc/SiliconForge/issues/22) (diagnostics de rendu) restent à réaliser. Les captures ne contiennent pas les décorations de fenêtre et leur encodage est externe. Jasmine consomme du JavaScript classique et ne nécessite pas l'environnement développeur. Le parcours navigateur et programmes natifs est qualifié en amont sous Linux/CEF ; les tests unitaires du runner couvrent aussi Windows et macOS. Les scripts `wf run` et `wf exec` ne disposent pas d'API de processus.

L'évaluation de `webfront-agent` 0.6.4 et de ses autorisations explicites reste à faire. Le tag bibliothèque 0.8.0 épingle encore l'environnement développeur 0.6.3 ; la publication séparée de 0.6.4 ne change pas ce pin.
